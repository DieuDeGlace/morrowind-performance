#include "opaqueblit.hpp"

#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

#include <components/stereo/multiview.hpp>
#include <components/stereo/stereomanager.hpp>

#ifdef ANDROID
#include <osg/BlendFunc>
#include <osg/Camera>
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

        // OPENMW_ANDROID_053_DEPTH_IN_SPARE_COLOR_COPY
        // On the GL4ES device water sampled the dedicated packed depth texture
        // as (0,0,0,1) whatever its slot, sampler precision or filtering, while
        // both opaque colour copies always sampled correctly, including when
        // they held packed depth. So the depth is packed into the colour copy
        // this frame does not use (the other one of the ping-pong pair), and
        // RenderingManager hands that texture to shaders as the opaque depth.
        if (const auto& packedFbo = mOpaqueFbo[1 - frameId])
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
            logPackedDepth(state, renderInfo, sceneDepth, packed);
            state.popStateSet();
        }
        else if (!mLoggedNoPackedFbo)
        {
            mLoggedNoPackedFbo = true;
            Log(Debug::Info) << "Android water diagnostic: the spare colour copy is not set up yet, so this frame's "
                                "water has no depth";
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
    AndroidWaterDiagnostic& AndroidWaterDiagnostic::get()
    {
        static AndroidWaterDiagnostic instance = [] {
            AndroidWaterDiagnostic diagnostic;
            diagnostic.mDebugUniform = new osg::Uniform("androidWaterDebug", 0.f);
            diagnostic.mDebugUniform->setDataVariance(osg::Object::DYNAMIC);
            return diagnostic;
        }();
        return instance;
    }

    void OpaqueColorBinCallback::logPackedDepth(
        osg::State& state, osg::RenderInfo& renderInfo, const osg::Texture* sceneDepth, const osg::Texture* packed)
    {
        static const unsigned int reportAt[] = { 600, 1800, 3600, 7200 };
        constexpr unsigned int reportCount = sizeof(reportAt) / sizeof(reportAt[0]);
        ++mCopyCount;
        const bool report = mDiagnosticReports < reportCount && mCopyCount == reportAt[mDiagnosticReports];

        // The report frame and the five after it switch water.frag to one of its
        // debug outputs; afterWater() reads the result back.
        const unsigned int frame = state.getFrameStamp()->getFrameNumber();
        if (report)
            mDebugBaseFrame = frame;
        int debugMode = 0;
        if (mDebugBaseFrame != ~0u && frame >= mDebugBaseFrame && frame - mDebugBaseFrame < 6)
            debugMode = static_cast<int>(frame - mDebugBaseFrame) + 1;
        AndroidWaterDiagnostic& diagnostic = AndroidWaterDiagnostic::get();
        diagnostic.mDebugUniform->set(static_cast<float>(debugMode));
        diagnostic.mPendingReadback = debugMode;

        if (!report)
            return;
        ++mDiagnosticReports;

        osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        const GLenum errorBefore = glGetError();
        const GLenum status
            = ext->glCheckFramebufferStatus ? ext->glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT) : GLenum(0);
        const int width = packed->getTextureWidth();
        const int height = packed->getTextureHeight();
        const char* noHighp = std::getenv("LIBGL_NOHIGHP");
        // GL4ES answers GL_MAX_TEXTURE_IMAGE_UNITS with the hardware count but
        // accepts glActiveTexture only below its own GL_MAX_TEXTURE_UNITS.
        GLint maxTextureUnits = 0;
        GLint maxTextureImageUnits = 0;
        glGetIntegerv(0x84E2 /* GL_MAX_TEXTURE_UNITS */, &maxTextureUnits);
        glGetIntegerv(0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS */, &maxTextureImageUnits);
        glGetError();
        Log(Debug::Info) << "Android water diagnostic: GL_MAX_TEXTURE_UNITS " << maxTextureUnits
                         << ", GL_MAX_TEXTURE_IMAGE_UNITS " << maxTextureImageUnits;

        Log(Debug::Info) << "Android water diagnostic " << mDiagnosticReports << "/" << reportCount << " (copy "
                         << mCopyCount << "): packed " << width << "x" << height << ", scene depth "
                         << sceneDepth->getTextureWidth() << "x" << sceneDepth->getTextureHeight()
                         << ", depth internal format 0x" << std::hex << sceneDepth->getInternalFormat()
                         << ", framebuffer status 0x" << status << ", GL error before read 0x" << errorBefore
                         << std::dec << ", LIBGL_NOHIGHP=" << (noHighp ? noHighp : "unset");

        if (width <= 0 || height <= 0)
            return;

        // Where the water plane would be at each sampled pixel, computed on the
        // CPU from the same camera, so the log shows whether water would see the
        // ground behind it as nearer than its own surface (which turns the
        // shore fade off through the depth guard in water.frag).
        const AndroidWaterDiagnostic& diag = AndroidWaterDiagnostic::get();
        const osg::Camera* camera = renderInfo.getCurrentCamera();
        const bool haveCamera = camera && diag.mHaveProjection;
        osg::Matrixd viewProjection;
        osg::Matrixd inverseViewProjection;
        if (haveCamera)
        {
            viewProjection = camera->getViewMatrix() * diag.mProjection;
            inverseViewProjection = osg::Matrixd::inverse(viewProjection);
            const osg::Vec3d eye = osg::Vec3d(0, 0, 0) * osg::Matrixd::inverse(camera->getViewMatrix());
            double fovy = 0, aspect = 0, projNear = 0, projFar = 0;
            const bool perspective = diag.mProjection.getPerspective(fovy, aspect, projNear, projFar);
            Log(Debug::Info) << "Android water diagnostic: camera \"" << camera->getName() << "\" eye "
                             << std::setprecision(7) << eye.x() << "," << eye.y() << "," << eye.z() << ", water level "
                             << diag.mWaterLevel << (diag.mHaveWaterLevel ? "" : " (unset)") << ", shader near/far "
                             << diag.mNear << "/" << diag.mFar << ", projection "
                             << (perspective ? "" : "not perspective ") << "near/far " << projNear << "/" << projFar
                             << " fovy " << fovy << " aspect " << aspect;
        }
        else
            Log(Debug::Info) << "Android water diagnostic: no camera or projection, water depth not computed";

        const auto linearize = [&diag](double d) {
            const double zn = 2.0 * d - 1.0;
            return 2.0 * diag.mNear * diag.mFar / (diag.mFar + diag.mNear - zn * (diag.mFar - diag.mNear));
        };

        // 3x3 grid; the bottom row is usually ground or water, the top row sky.
        const float fractions[] = { 0.1f, 0.5f, 0.9f };
        for (float fy : fractions)
        {
            std::ostringstream row;
            row << std::setprecision(7) << "Android water diagnostic: row " << static_cast<int>(fy * 100.f) << "% up:";
            for (float fx : fractions)
            {
                const GLint x = std::min(width - 1, static_cast<int>(fx * width));
                const GLint y = std::min(height - 1, static_cast<int>(fy * height));
                unsigned char rgba[4] = { 0, 0, 0, 0 };
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                const double depth = (rgba[0] * 65536.0 + rgba[1] * 256.0 + rgba[2]) / 16777215.0;
                row << " (" << int(rgba[0]) << "," << int(rgba[1]) << "," << int(rgba[2]) << "," << int(rgba[3])
                    << ")=" << depth;

                if (!haveCamera || !diag.mHaveWaterLevel)
                    continue;
                const double nx = 2.0 * (x + 0.5) / width - 1.0;
                const double ny = 2.0 * (y + 0.5) / height - 1.0;
                const osg::Vec4d nearPoint = osg::Vec4d(nx, ny, -1.0, 1.0) * inverseViewProjection;
                const osg::Vec4d farPoint = osg::Vec4d(nx, ny, 1.0, 1.0) * inverseViewProjection;
                const osg::Vec3d a(
                    nearPoint.x() / nearPoint.w(), nearPoint.y() / nearPoint.w(), nearPoint.z() / nearPoint.w());
                const osg::Vec3d b(
                    farPoint.x() / farPoint.w(), farPoint.y() / farPoint.w(), farPoint.z() / farPoint.w());
                const double dz = b.z() - a.z();
                const double t = dz != 0.0 ? (diag.mWaterLevel - a.z()) / dz : -1.0;
                if (t <= 0.0 || t >= 1.0)
                {
                    row << " [no water]";
                    continue;
                }
                const osg::Vec3d hit = a + (b - a) * t;
                const osg::Vec4d clip = osg::Vec4d(hit, 1.0) * viewProjection;
                const double waterDepth = 0.5 * clip.z() / clip.w() + 0.5;
                const double opaqueLinear = linearize(depth);
                const double waterLinear = linearize(waterDepth);
                row << " [water " << waterDepth << ", linear " << opaqueLinear << " vs " << waterLinear
                    << (opaqueLinear >= waterLinear * 0.9 ? ", fade ok]" : ", guard fires]");
            }
            Log(Debug::Info) << row.str();
        }
        const GLenum errorAfter = glGetError();
        if (errorAfter != GL_NO_ERROR)
            Log(Debug::Warning) << "Android water diagnostic: reading the packed depth raised GL error 0x" << std::hex
                                << errorAfter;
    }

    // OPENMW_ANDROID_053_WATER_DEBUG_READBACK
    // Runs right after the water leaves are drawn into the scene framebuffer.
    void OpaqueColorBinCallback::afterWater(osg::RenderInfo& renderInfo)
    {
        AndroidWaterDiagnostic& diagnostic = AndroidWaterDiagnostic::get();
        const int mode = diagnostic.mPendingReadback;
        if (mode == 0)
            return;
        diagnostic.mPendingReadback = 0;

        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int width = viewport[2];
        const int height = viewport[3];
        static const char* const modeNames[] = { "", "raw packed texel", "distances and flags",
            "gl_FragCoord distance and screen coordinates", "opaque colour sample",
            "packed texel alpha, centre texel red and green", "opaque colour sample, repeated" };
        if (mode < 1 || mode > 6)
            return;
        Log(Debug::Info) << "Android water shader output, mode " << mode << " (" << modeNames[mode] << "), viewport "
                         << viewport[0] << "," << viewport[1] << " " << width << "x" << height;
        if (width <= 0 || height <= 0)
            return;

        if (mode == 1)
        {
            // Which textures GL has on the water's slots, against the copies'
            // texture names, to catch a stale or missing binding.
            const unsigned int contextId = renderInfo.getState()->getContextID();
            const auto textureName = [contextId](const osg::ref_ptr<osg::FrameBufferObject>& fbo) -> GLuint {
                if (!fbo)
                    return 0;
                const osg::Texture* texture
                    = fbo->getAttachment(osg::FrameBufferObject::BufferComponent::COLOR_BUFFER0).getTexture();
                const osg::Texture::TextureObject* object = texture ? texture->getTextureObject(contextId) : nullptr;
                return object ? object->id() : 0;
            };
            constexpr GLenum texture0 = 0x84C0; // GL_TEXTURE0
            osg::GLExtensions* ext = renderInfo.getState()->get<osg::GLExtensions>();
            GLint activeUnit = texture0;
            glGetIntegerv(0x84E0 /* GL_ACTIVE_TEXTURE */, &activeUnit);
            GLint bound[2] = { 0, 0 };
            for (int i = 0; i < 2; ++i)
            {
                ext->glActiveTexture(texture0 + 2 + i);
                glGetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &bound[i]);
            }
            ext->glActiveTexture(static_cast<GLenum>(activeUnit));
            Log(Debug::Info) << "Android water diagnostic: slot 2 holds texture " << bound[0] << ", slot 3 holds "
                             << bound[1] << "; colour copies " << textureName(mOpaqueFbo[0]) << "/"
                             << textureName(mOpaqueFbo[1])
                             << " (slot 3 should hold the copy for the other parity), frame parity "
                             << renderInfo.getState()->getFrameStamp()->getFrameNumber() % 2;
        }

        const float fractions[] = { 0.1f, 0.5f, 0.9f };
        for (float fy : fractions)
        {
            std::ostringstream row;
            row << std::setprecision(7) << "Android water shader output: row " << static_cast<int>(fy * 100.f)
                << "% up:";
            for (float fx : fractions)
            {
                const GLint x = viewport[0] + std::min(width - 1, static_cast<int>(fx * width));
                const GLint y = viewport[1] + std::min(height - 1, static_cast<int>(fy * height));
                unsigned char rgba[4] = { 0, 0, 0, 0 };
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                row << " (" << int(rgba[0]) << "," << int(rgba[1]) << "," << int(rgba[2]) << "," << int(rgba[3]) << ")";
                if (mode == 1 || mode == 6)
                    row << "=" << (rgba[0] * 65536.0 + rgba[1] * 256.0 + rgba[2]) / 16777215.0;
                else if (mode == 5)
                    row << "=[alpha " << int(rgba[0]) << ", centre (" << int(rgba[1]) << "," << int(rgba[2]) << ")]";
                else if (mode == 2)
                {
                    const int flags = rgba[2];
                    row << "=[opaque " << rgba[0] * 4096.0 / 255.0 << ", surface " << rgba[1] * 4096.0 / 255.0
                        << ((flags & 128) ? ", highp" : ", no highp") << ((flags & 64) ? ", depth disabled" : "")
                        << ((flags & 32) ? ", guard fires" : "") << ((flags & 16) ? ", above water" : "")
                        << ((flags & 8) ? ", GL ES" : "") << "]";
                }
                else if (mode == 3)
                    row << "=[fragcoord distance " << rgba[0] * 4096.0 / 255.0 << ", uv " << rgba[1] / 255.0 << ","
                        << rgba[2] / 255.0 << "]";
            }
            Log(Debug::Info) << row.str();
        }
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            Log(Debug::Warning) << "Android water shader output: reading back raised GL error 0x" << std::hex << error;
    }
#endif
}
