#include "opaqueblit.hpp"

#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

#include <components/stereo/multiview.hpp>
#include <components/stereo/stereomanager.hpp>

#ifdef ANDROID
#include <osg/BlendFunc>
#include <osg/ColorMask>
#include <osg/Depth>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Uniform>
#include <osg/Viewport>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

#include <components/debug/debuglog.hpp>

extern "C" int gl4es_getDepthCopySupport() __attribute__((weak));
#endif

namespace MWRender
{
    void OpaqueColorBinCallback::drawImplementation(
        osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous)
    {
        resolve(bin, renderInfo, previous);
        bin->drawImplementation(renderInfo, previous);
    }

    void OpaqueColorBinCallback::resolve(
        osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, [[maybe_unused]] osgUtil::RenderLeaf* previous)
    {
        osg::State& state = *renderInfo.getState();
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        const unsigned int frameId = state.getFrameStamp()->getFrameNumber() % 2;
        const auto& fbo = mFbo[frameId];
        const auto& msaaFbo = mMsaaFbo[frameId];
        const auto& opaqueFbo = mOpaqueFbo[frameId];

        if (!bin->getStage()->getFrameBufferObject()
            || (bin->getStage()->getFrameBufferObject() != fbo && bin->getStage()->getFrameBufferObject() != msaaFbo))
        {
#ifdef ANDROID
            if (!mLogged)
            {
                mLogged = true;
                Log(Debug::Warning) << "Android water: the scene is not drawn into the post-processing framebuffer, "
                                       "so refraction and shore depth have no scene copy";
            }
#endif
            return;
        }

        const osg::Texture* colorTex
            = opaqueFbo->getAttachment(osg::FrameBufferObject::BufferComponent::COLOR_BUFFER0).getTexture();

#ifdef ANDROID
        // OPENMW_ANDROID_053_OPAQUE_COPY
        // GL4ES has no usable glBlitFramebuffer; the 0.51 Android port already
        // disabled every blit. Copy the opaque scene by drawing it instead.
        const osg::Texture* sceneColor
            = fbo->getAttachment(osg::FrameBufferObject::BufferComponent::COLOR_BUFFER0).getTexture();
        const osg::Texture* sceneDepth
            = fbo->getAttachment(osg::FrameBufferObject::BufferComponent::PACKED_DEPTH_STENCIL_BUFFER).getTexture();
        if (msaaFbo || !sceneColor || !sceneDepth)
        {
            if (!mLogged)
            {
                mLogged = true;
                Log(Debug::Warning) << "Android water: cannot copy the opaque scene "
                                    << (msaaFbo ? "from a multisampled framebuffer" : "without scene textures");
            }
            return;
        }
        drawCopy(state, renderInfo, previous, frameId, sceneColor, sceneDepth, colorTex);
        return;
#endif

        if (Stereo::getMultiview())
        {
            if (!mMultiviewResolve[frameId])
                mMultiviewResolve[frameId] = std::make_unique<Stereo::MultiviewFramebufferResolve>(
                    msaaFbo ? msaaFbo : fbo, opaqueFbo, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            else
            {
                mMultiviewResolve[frameId]->setResolveFbo(opaqueFbo);
                mMultiviewResolve[frameId]->setMsaaFbo(msaaFbo ? msaaFbo : fbo);
            }
            mMultiviewResolve[frameId]->resolveImplementation(state);
        }
        else
        {
            (msaaFbo ? msaaFbo : fbo)->apply(state, osg::FrameBufferObject::READ_FRAMEBUFFER);
            opaqueFbo->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
            glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
            ext->glBlitFramebuffer(0, 0, colorTex->getTextureWidth(), colorTex->getTextureHeight(), 0, 0,
                colorTex->getTextureWidth(), colorTex->getTextureHeight(), GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                GL_NEAREST);
        }

        (msaaFbo ? msaaFbo : fbo)->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
    }

#ifdef ANDROID
    namespace
    {
        osg::Program* makeCopyProgram(bool copyDepth)
        {
            auto* program = new osg::Program;
            program->addShader(new osg::Shader(osg::Shader::VERTEX, R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 copyUv;
#else
varying vec2 copyUv;
#endif
void main()
{
    gl_Position = gl_Vertex;
    copyUv = gl_Vertex.xy * 0.5 + 0.5;
}
)glsl"));
            std::string fragment = R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 copyUv;
#else
varying vec2 copyUv;
#endif
uniform sampler2D sceneColor;
#if defined(GL_ES) && defined(GL_FRAGMENT_PRECISION_HIGH)
uniform highp sampler2D sceneDepth;
#else
uniform sampler2D sceneDepth;
#endif
void main()
{
    gl_FragColor = texture2D(sceneColor, copyUv);
)glsl";
            if (copyDepth)
                fragment += "    gl_FragDepth = texture2D(sceneDepth, copyUv).r;\n";
            fragment += "}\n";
            program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragment));
            return program;
        }

        // OPENMW_ANDROID_053_PACKED_OPAQUE_DEPTH
        // Many GLES2 drivers cannot write gl_FragDepth, so the opaque depth copy
        // above was left cleared to "far" and water treated every pixel as deep
        // open water (no shore fade or wobbly shores). Any GLES2 device can
        // sample the scene depth texture, so store it as a 24-bit integer in
        // RGB instead; sampleOpaqueDepthTex unpacks it. A depth of 1.0 packs to
        // white and unpacks to exactly 1.0, so the sky still reads as empty.
        osg::Program* makePackProgram()
        {
            auto* program = new osg::Program;
            program->addShader(new osg::Shader(osg::Shader::VERTEX, R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 copyUv;
#else
varying vec2 copyUv;
#endif
void main()
{
    gl_Position = gl_Vertex;
    copyUv = gl_Vertex.xy * 0.5 + 0.5;
}
)glsl"));
            program->addShader(new osg::Shader(osg::Shader::FRAGMENT, R"glsl(#version 120
#if defined(GL_ES)
varying highp vec2 copyUv;
uniform highp sampler2D sceneDepth;
#define HIGHP highp
#else
varying vec2 copyUv;
uniform sampler2D sceneDepth;
#define HIGHP
#endif
void main()
{
    // Clamp after rounding: in 32-bit floats 16777215.0 + 0.5 rounds up to 2^24.
    HIGHP float value = min(floor(clamp(texture2D(sceneDepth, copyUv).r, 0.0, 1.0) * 16777215.0 + 0.5), 16777215.0);
    HIGHP float red = floor(value / 65536.0);
    value -= red * 65536.0;
    HIGHP float green = floor(value / 256.0);
    HIGHP float blue = value - green * 256.0;
    gl_FragColor = vec4(red, green, blue, 255.0) / 255.0;
}
)glsl"));
            return program;
        }
    }

    void OpaqueColorBinCallback::drawCopy(osg::State& state, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf* previous,
        unsigned int frameId, const osg::Texture* sceneColor, const osg::Texture* sceneDepth,
        const osg::Texture* opaqueColor)
    {
        const bool copyDepth = gl4es_getDepthCopySupport && gl4es_getDepthCopySupport();
        if (!mCopyGeometry)
        {
            mCopyGeometry = new osg::Geometry;
            mCopyGeometry->setUseDisplayList(false);
            mCopyGeometry->setUseVertexBufferObjects(true);
            auto* vertices = new osg::Vec3Array;
            vertices->push_back(osg::Vec3(-1, -1, 0));
            vertices->push_back(osg::Vec3(3, -1, 0));
            vertices->push_back(osg::Vec3(-1, 3, 0));
            mCopyGeometry->setVertexArray(vertices);
            mCopyGeometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
            Log(Debug::Info) << "Android water: copying the opaque scene by drawing ("
                             << (copyDepth ? "colour and depth" : "colour only; GL4ES has no accurate depth copy")
                             << ")";
        }

        osg::ref_ptr<osg::StateSet>& stateSet = mCopyStateSet[frameId];
        if (!stateSet)
        {
            stateSet = new osg::StateSet;
            const auto on = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            const auto off = osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
            static const osg::ref_ptr<osg::Program> program = makeCopyProgram(copyDepth);
            stateSet->setAttributeAndModes(program, on);
            stateSet->addUniform(new osg::Uniform("sceneColor", 0), on);
            stateSet->addUniform(new osg::Uniform("sceneDepth", 1), on);
            stateSet->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, copyDepth), on);
            stateSet->setAttributeAndModes(new osg::ColorMask(true, true, true, true), on);
            stateSet->setMode(GL_DEPTH_TEST, on);
            stateSet->setMode(GL_BLEND, off);
            stateSet->setMode(GL_CULL_FACE, off);
            stateSet->setMode(GL_SCISSOR_TEST, off);
            stateSet->setMode(GL_ALPHA_TEST, off);
            stateSet->setMode(GL_POLYGON_OFFSET_FILL, off);
        }
        const auto on = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
        // The scene textures stay owned by the post-processor; binding does not modify them.
        stateSet->setTextureAttribute(0, const_cast<osg::Texture*>(sceneColor), on);
        stateSet->setTextureAttribute(1, const_cast<osg::Texture*>(sceneDepth), on);
        stateSet->setAttribute(
            new osg::Viewport(0, 0, opaqueColor->getTextureWidth(), opaqueColor->getTextureHeight()), on);

        if (previous)
            osgUtil::StateGraph::moveToRootStateGraph(state, previous->_parent->_parent);
        state.pushStateSet(stateSet);
        state.apply();

        mOpaqueFbo[frameId]->apply(state, osg::FrameBufferObject::READ_DRAW_FRAMEBUFFER);
        if (!copyDepth)
        {
            // Without an accurate depth copy, treat every refracted pixel as open
            // water rather than sampling stale depth from earlier frames.
            glDepthMask(GL_TRUE);
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);
            glDepthMask(GL_FALSE);
        }
        mCopyGeometry->draw(renderInfo);
        state.popStateSet();

        if (const auto& packedFbo = mPackedDepthFbo[frameId])
        {
            const osg::Texture* packed
                = packedFbo->getAttachment(osg::FrameBufferObject::BufferComponent::COLOR_BUFFER0).getTexture();
            osg::ref_ptr<osg::StateSet>& packSet = mPackStateSet[frameId];
            if (!packSet)
            {
                packSet = new osg::StateSet;
                const auto off
                    = osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED;
                static const osg::ref_ptr<osg::Program> program = makePackProgram();
                packSet->setAttributeAndModes(program, on);
                packSet->addUniform(new osg::Uniform("sceneDepth", 0), on);
                packSet->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, false), on);
                packSet->setAttributeAndModes(new osg::ColorMask(true, true, true, true), on);
                packSet->setMode(GL_DEPTH_TEST, off);
                packSet->setMode(GL_BLEND, off);
                packSet->setMode(GL_CULL_FACE, off);
                packSet->setMode(GL_SCISSOR_TEST, off);
                packSet->setMode(GL_ALPHA_TEST, off);
                packSet->setMode(GL_POLYGON_OFFSET_FILL, off);
            }
            packSet->setTextureAttribute(0, const_cast<osg::Texture*>(sceneDepth), on);
            packSet->setAttribute(new osg::Viewport(0, 0, packed->getTextureWidth(), packed->getTextureHeight()), on);
            state.pushStateSet(packSet);
            state.apply();
            packedFbo->apply(state, osg::FrameBufferObject::READ_DRAW_FRAMEBUFFER);
            mCopyGeometry->draw(renderInfo);
            state.popStateSet();
            logPackedDepth(state, sceneDepth, packed);
        }
        else if (!mLoggedNoPackedFbo)
        {
            mLoggedNoPackedFbo = true;
            Log(Debug::Warning) << "Android water diagnostic: no packed depth framebuffer, so water has no depth";
        }

        mFbo[frameId]->apply(state, osg::FrameBufferObject::READ_DRAW_FRAMEBUFFER);
        state.apply();
        if (previous)
        {
            osgUtil::StateGraph::moveStateGraph(state, nullptr, previous->_parent->_parent);
            state.apply(previous->_parent->getStateSet());
        }
    }

    // OPENMW_ANDROID_053_PACKED_DEPTH_DIAGNOSTIC
    // Logs what water will read as the depth below it, a few times per session,
    // so a missing shore fade can be traced to the depth copy on the device.
    // Must run while the packed framebuffer is bound for reading.
    void OpaqueColorBinCallback::logPackedDepth(
        osg::State& state, const osg::Texture* sceneDepth, const osg::Texture* packed)
    {
        static const unsigned int reportAt[] = { 120, 600, 1800, 3600 };
        constexpr unsigned int reportCount = sizeof(reportAt) / sizeof(reportAt[0]);
        ++mCopyCount;
        if (mDiagnosticReports >= reportCount || mCopyCount != reportAt[mDiagnosticReports])
            return;
        ++mDiagnosticReports;

        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        const GLenum errorBefore = glGetError();
        const GLenum status
            = ext->glCheckFramebufferStatus ? ext->glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT) : GLenum(0);
        const int width = packed->getTextureWidth();
        const int height = packed->getTextureHeight();
        const char* noHighp = std::getenv("LIBGL_NOHIGHP");

        Log(Debug::Info) << "Android water diagnostic " << mDiagnosticReports << "/" << reportCount << " (copy "
                         << mCopyCount << "): packed " << width << "x" << height << ", scene depth "
                         << sceneDepth->getTextureWidth() << "x" << sceneDepth->getTextureHeight()
                         << ", depth internal format 0x" << std::hex << sceneDepth->getInternalFormat()
                         << ", framebuffer status 0x" << status << ", GL error before read 0x" << errorBefore
                         << std::dec << ", LIBGL_NOHIGHP=" << (noHighp ? noHighp : "unset");

        if (width <= 0 || height <= 0)
            return;

        // 3x3 grid; the bottom row is usually ground or water, the top row sky.
        const float fractions[] = { 0.1f, 0.5f, 0.9f };
        for (float fy : fractions)
        {
            std::ostringstream row;
            row << "Android water diagnostic: row " << static_cast<int>(fy * 100.f) << "% up:";
            for (float fx : fractions)
            {
                const GLint x = std::min(width - 1, static_cast<int>(fx * width));
                const GLint y = std::min(height - 1, static_cast<int>(fy * height));
                unsigned char rgba[4] = { 0, 0, 0, 0 };
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                const double depth = (rgba[0] * 65536.0 + rgba[1] * 256.0 + rgba[2]) / 16777215.0;
                row << " (" << int(rgba[0]) << "," << int(rgba[1]) << "," << int(rgba[2]) << "," << int(rgba[3])
                    << ")=" << std::setprecision(7) << depth;
            }
            Log(Debug::Info) << row.str();
        }
        const GLenum errorAfter = glGetError();
        if (errorAfter != GL_NO_ERROR)
            Log(Debug::Warning) << "Android water diagnostic: reading the packed depth raised GL error 0x" << std::hex
                                << errorAfter;
    }
#endif
}
