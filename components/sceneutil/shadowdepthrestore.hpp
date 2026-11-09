#ifndef OPENMW_SCENEUTIL_SHADOWDEPTHRESTORE_H
#define OPENMW_SCENEUTIL_SHADOWDEPTHRESTORE_H

#include <osg/ColorMask>
#include <osg/Depth>
#include <osg/FrameBufferObject>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Scissor>
#include <osg/Shader>
#include <osg/State>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Vec4i>

namespace SceneUtil
{
    // Save scenery only beneath moving casters. Reused frames restore that
    // background before current casters are drawn into the complete map.
    class ShadowDepthRestore final : public osg::Geometry
    {
    public:
        ShadowDepthRestore(osg::Texture2D* background, osg::Texture2D* complete)
            : mDepth(background)
            , mComplete(complete)
        {
            mFramebuffer = new osg::FrameBufferObject;
            mFramebuffer->setAttachment(osg::Camera::DEPTH_BUFFER, osg::FrameBufferAttachment(background));
            setUseDisplayList(false);
            setUseVertexBufferObjects(true);
            setNodeMask(0);
            auto vertices = new osg::Vec3Array;
            vertices->push_back(osg::Vec3(-1, -1, 0));
            vertices->push_back(osg::Vec3(3, -1, 0));
            vertices->push_back(osg::Vec3(-1, 3, 0));
            setVertexArray(vertices);
            addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
            auto* state = getOrCreateStateSet();
            const auto on = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            const auto off = osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            state->setRenderBinDetails(-100, "RenderBin", osg::StateSet::OVERRIDE_PROTECTED_RENDERBIN_DETAILS);
            state->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, true), on);
            state->setAttributeAndModes(new osg::ColorMask(false, false, false, false), on);
            state->setAttributeAndModes(
                new osg::Scissor(0, 0, background->getTextureWidth(), background->getTextureHeight()), on);
            state->setMode(GL_CULL_FACE, off);
            state->setMode(GL_BLEND, off);
            state->setMode(GL_ALPHA_TEST, off);
            state->setMode(GL_POLYGON_OFFSET_FILL, off);
            state->addUniform(new osg::Uniform("cachedDepth", 0), on);
            static const osg::ref_ptr<osg::Program> program = makeProgram();
            state->setAttributeAndModes(program, on);
        }

        void setRegions(const osg::Vec4i& previous, const osg::Vec4i& current)
        {
            mPrevious = previous;
            mCurrent = current;
        }

        void drawImplementation(osg::RenderInfo& info) const override
        {
            if (empty(mPrevious) && empty(mCurrent))
                return;
            auto& state = *info.getState();
            // Compile/bind through the tracked texture unit before FBO setup.
            // OSG's FBO texture allocation otherwise bypasses State's binding cache.
            state.setActiveTextureUnit(0);
            mDepth->apply(state);
            state.haveAppliedTextureAttribute(0, mDepth);
            auto* extensions = state.get<osg::GLExtensions>();
#ifndef ANDROID
            if (extensions->glBlitFramebuffer)
            {
                GLint read = 0, draw = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &read);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT, &draw);
                if (!empty(mPrevious))
                {
                    mFramebuffer->apply(state, osg::FrameBufferObject::READ_FRAMEBUFFER);
                    glReadBuffer(GL_NONE);
                    blit(*extensions, mPrevious);
                }
                if (!empty(mCurrent))
                {
                    extensions->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, draw);
                    glReadBuffer(GL_NONE);
                    mFramebuffer->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
                    blit(*extensions, mCurrent);
                }
                extensions->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, read);
                extensions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, draw);
                return;
            }
#endif
            GLint framebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &framebuffer);
            if (!empty(mPrevious))
            {
                state.setActiveTextureUnit(0);
                mDepth->apply(state);
                state.haveAppliedTextureAttribute(0, mDepth);
                drawRegion(info, mPrevious);
            }
            if (!empty(mCurrent))
            {
                mFramebuffer->apply(state);
                state.setActiveTextureUnit(0);
                mComplete->apply(state);
                state.haveAppliedTextureAttribute(0, mComplete);
#ifndef ANDROID
                if (mComplete->getShadowComparison())
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE);
#endif
                drawRegion(info, mCurrent);
#ifndef ANDROID
                if (mComplete->getShadowComparison())
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB);
#endif
                extensions->glBindFramebuffer(GL_FRAMEBUFFER_EXT, framebuffer);
            }
            glScissor(0, 0, mDepth->getTextureWidth(), mDepth->getTextureHeight());
        }

        void releaseGLObjects(osg::State* state) const override
        {
            osg::Geometry::releaseGLObjects(state);
            mFramebuffer->releaseGLObjects(state);
        }

    private:
        static bool empty(const osg::Vec4i& region) { return region.z() == 0 || region.w() == 0; }
        static void blit(osg::GLExtensions& gl, const osg::Vec4i& r)
        {
            gl.glBlitFramebuffer(r.x(), r.y(), r.x() + r.z(), r.y() + r.w(), r.x(), r.y(), r.x() + r.z(), r.y() + r.w(),
                GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        }
        void drawRegion(osg::RenderInfo& info, const osg::Vec4i& r) const
        {
            glScissor(r.x(), r.y(), r.z(), r.w());
            osg::Geometry::drawImplementation(info);
        }
        static osg::Program* makeProgram()
        {
            auto* program = new osg::Program;
            program->addShader(new osg::Shader(osg::Shader::VERTEX, R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 depthUv;
#else
varying vec2 depthUv;
#endif
void main()
{
    gl_Position = gl_Vertex;
    depthUv = gl_Vertex.xy * 0.5 + 0.5;
}
)glsl"));
            program->addShader(new osg::Shader(osg::Shader::FRAGMENT, R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 depthUv;
#else
varying vec2 depthUv;
#endif
#if defined(GL_ES) && defined(GL_FRAGMENT_PRECISION_HIGH)
uniform highp sampler2D cachedDepth;
#else
uniform sampler2D cachedDepth;
#endif
void main()
{
    gl_FragDepth = texture2D(cachedDepth, depthUv).r;
}
)glsl"));
            return program;
        }

        osg::ref_ptr<osg::Texture2D> mDepth, mComplete;
        osg::ref_ptr<osg::FrameBufferObject> mFramebuffer;
        osg::Vec4i mPrevious, mCurrent;
    };
}
#endif
