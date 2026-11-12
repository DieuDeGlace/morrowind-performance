#ifndef OPENMW_MWRENDER_OPAQUEBLIT_H
#define OPENMW_MWRENDER_OPAQUEBLIT_H

#include <array>
#include <memory>

#include <osg/FrameBufferObject>
#include <osgUtil/RenderBin>

#ifdef ANDROID
#include <osg/Geometry>
#include <osg/Matrixd>
#include <osg/StateSet>
#include <osg/Uniform>
#endif

namespace Stereo
{
    class MultiviewFramebufferResolve;
}

namespace MWRender
{
#ifdef ANDROID
    // OPENMW_ANDROID_053_PACKED_DEPTH_DIAGNOSTIC
    // What the water shader is given, recorded for the depth diagnostic log.
    struct AndroidWaterDiagnostic
    {
        static AndroidWaterDiagnostic& get();

        osg::Matrixd mProjection;
        bool mHaveProjection = false;
        double mNear = 0.0;
        double mFar = 0.0;
        double mWaterLevel = 0.0;
        bool mHaveWaterLevel = false;
        // OPENMW_ANDROID_053_WATER_DEBUG_READBACK: water.frag's androidWaterDebug.
        // 1 outputs the raw packed texel, 2 the depths and branch flags, 3 the
        // gl_FragCoord depth and screen coordinates; read back after the water.
        osg::ref_ptr<osg::Uniform> mDebugUniform;
        int mPendingReadback = 0;
    };
#endif

    class OpaqueColorBinCallback : public osgUtil::RenderBin::DrawCallback
    {
    public:
        void drawImplementation(osgUtil::RenderBin*, osg::RenderInfo&, osgUtil::RenderLeaf*&) override;
        // previous is the last leaf drawn; its state is restored if the copy changes state.
        void resolve(osgUtil::RenderBin*, osg::RenderInfo&, osgUtil::RenderLeaf* previous);

        osg::ref_ptr<osg::FrameBufferObject> mFbo[2];
        osg::ref_ptr<osg::FrameBufferObject> mMsaaFbo[2];
        osg::ref_ptr<osg::FrameBufferObject> mOpaqueFbo[2];
        std::array<std::unique_ptr<Stereo::MultiviewFramebufferResolve>, 2> mMultiviewResolve;
#ifdef ANDROID
        // Receives the opaque depth packed into RGB, which every shader reads.
        osg::ref_ptr<osg::FrameBufferObject> mPackedDepthFbo[2];
        // Called right after the water is drawn; logs its debug output, if any.
        void afterWater(osg::RenderInfo& renderInfo);
#endif

#ifdef ANDROID
    private:
        // GL4ES cannot blit framebuffers, so the copy is drawn instead.
        void drawCopy(osg::State& state, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf* previous,
            unsigned int frameId, const osg::Texture* sceneColor, const osg::Texture* sceneDepth,
            const osg::Texture* opaqueColor);

        osg::ref_ptr<osg::Geometry> mCopyGeometry;
        osg::ref_ptr<osg::StateSet> mCopyStateSet[2];
        osg::ref_ptr<osg::StateSet> mPackStateSet[2];
        bool mLogged = false;
        // OPENMW_ANDROID_053_PACKED_DEPTH_DIAGNOSTIC
        void logPackedDepth(
            osg::State& state, osg::RenderInfo& renderInfo, const osg::Texture* sceneDepth, const osg::Texture* packed);
        unsigned int mCopyCount = 0;
        unsigned int mDiagnosticReports = 0;
        unsigned int mDebugBaseFrame = ~0u;
        bool mLoggedNoPackedFbo = false;
#endif
    };
}

#endif
