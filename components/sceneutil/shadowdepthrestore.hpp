#ifndef OPENMW_SCENEUTIL_SHADOWDEPTHRESTORE_H
#define OPENMW_SCENEUTIL_SHADOWDEPTHRESTORE_H

#include <osg/ColorMask>
#include <osg/Depth>
#include <osg/FrameBufferObject>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/State>
#include <osg/Texture2D>
#include <osg/Uniform>

namespace SceneUtil
{
    // Restore cached scenery depth before drawing this frame's moving casters.
    // Receiver shaders keep sampling one complete map with their existing filter.
    class ShadowDepthRestore final : public osg::Geometry
    {
    public:
        explicit ShadowDepthRestore(osg::Texture2D* depth)
            : mDepth(depth)
        {
            mFramebuffer = new osg::FrameBufferObject;
            mFramebuffer->setAttachment(osg::Camera::DEPTH_BUFFER, osg::FrameBufferAttachment(depth));
            setUseDisplayList(false);
            setUseVertexBufferObjects(true);
            // Inserted directly by the shadow cull callback; it has no world bounds.
            setNodeMask(0);
            auto vertices = new osg::Vec3Array;
            vertices->push_back(osg::Vec3(-1, -1, 0));
            vertices->push_back(osg::Vec3(3, -1, 0));
            vertices->push_back(osg::Vec3(-1, 3, 0));
            setVertexArray(vertices);
            addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));

            auto* state = getOrCreateStateSet();
            const auto protectedOn
                = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            const auto protectedOff
                = osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            state->setRenderBinDetails(-100, "RenderBin", osg::StateSet::OVERRIDE_PROTECTED_RENDERBIN_DETAILS);
            state->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, true), protectedOn);
            state->setAttributeAndModes(new osg::ColorMask(false, false, false, false), protectedOn);
            state->setMode(GL_CULL_FACE, protectedOff);
            state->setMode(GL_BLEND, protectedOff);
            state->setMode(GL_ALPHA_TEST, protectedOff);
            state->setMode(GL_POLYGON_OFFSET_FILL, protectedOff);
            state->setTextureAttributeAndModes(0, depth, protectedOn);
            state->addUniform(new osg::Uniform("cachedDepth", 0), protectedOn);

            static const osg::ref_ptr<osg::Program> program = makeProgram();
            state->setAttributeAndModes(program, protectedOn);
        }

        void drawImplementation(osg::RenderInfo& info) const override
        {
#ifndef ANDROID
            auto& state = *info.getState();
            auto* extensions = state.get<osg::GLExtensions>();
            if (extensions->glBlitFramebuffer)
            {
                GLint previousRead = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT, &previousRead);
                mFramebuffer->apply(state, osg::FrameBufferObject::READ_FRAMEBUFFER);
                glReadBuffer(GL_NONE);
                const int width = mDepth->getTextureWidth();
                const int height = mDepth->getTextureHeight();
                extensions->glBlitFramebuffer(
                    0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
                extensions->glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, previousRead);
                return;
            }
#endif
            osg::Geometry::drawImplementation(info);
        }

        void releaseGLObjects(osg::State* state) const override
        {
            osg::Geometry::releaseGLObjects(state);
            mFramebuffer->releaseGLObjects(state);
        }

    private:
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

        osg::ref_ptr<osg::Texture2D> mDepth;
        osg::ref_ptr<osg::FrameBufferObject> mFramebuffer;
    };
}

#endif
