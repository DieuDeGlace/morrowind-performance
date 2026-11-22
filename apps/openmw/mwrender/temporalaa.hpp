#ifndef OPENMW_MWRENDER_TEMPORALAA_H
#define OPENMW_MWRENDER_TEMPORALAA_H

#include <array>

#include <osg/FrameBufferObject>
#include <osg/Matrixd>
#include <osg/Referenced>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Vec2f>
#include <osg/Vec3d>
#include <osg/Viewport>

namespace osg
{
    class RenderInfo;
}

namespace MWRender
{
    class PingPongCanvas;

    // Temporal anti-aliasing, after the Infinite Cubes temporal resolve.
    //
    // Each frame the scene projection is shifted by a sub-pixel Halton offset
    // (jitter). The resolve pass, which runs before any post-processing shader,
    // reprojects every pixel into the previous frame using the scene depth and
    // the previous frame's view-projection, checks the previous depth to make
    // sure it is the same surface, and blends the clipped history with the
    // current frame. History lives on the unjittered pixel grid.
    //
    // Threading: advanceJitter() and cull() run on the main thread (update,
    // then cull, of the same frame); resolve() runs on the draw thread. Per-frame
    // data is double-buffered by frame parity, like the post-processing canvases.
    class TemporalAA : public osg::Referenced
    {
    public:
        explicit TemporalAA(bool reversedZ);

        // Setting enabled and the renderer supports it (no stereo).
        bool isEnabled() const;

        // Main thread, once per frame before culling: the pixel offset to apply
        // to the scene projection this frame, (0, 0) when disabled.
        osg::Vec2f advanceJitter();

        // The unjittered projection the scene depth is encoded with.
        void setProjection(const osg::Matrixd& projection) { mProjection = projection; }

        // Main thread, scene camera cull: per-frame matrices and textures.
        void cull(unsigned frameId, const osg::Matrixd& view, const osg::Vec3d& eye, osg::Texture* depth,
            osg::Texture* previousDepth, int width, int height);

        // Draw thread: resolves into a history buffer and returns it, or returns
        // the scene unchanged when temporal anti-aliasing is not active.
        osg::Texture* resolve(osg::RenderInfo& renderInfo, const PingPongCanvas& canvas, osg::Texture* scene,
            unsigned frameId, unsigned frameNumber) const;

        // Forget the history (resize, renderer reset).
        void reset() { mResetRequested = true; }

    private:
        struct Frame
        {
            osg::ref_ptr<osg::StateSet> mStateSet;
            osg::ref_ptr<osg::Uniform> mClipToPrevious;
            osg::ref_ptr<osg::Uniform> mMetrics;
            osg::ref_ptr<osg::Uniform> mJitter;
            osg::ref_ptr<osg::Uniform> mTuning;
            osg::ref_ptr<osg::Uniform> mDepthParams;
            osg::ref_ptr<osg::Viewport> mViewport;
            osg::ref_ptr<osg::Texture> mDepth;
            osg::ref_ptr<osg::Texture> mPreviousDepth;
            bool mActive = false;
            bool mReproject = false;
        };

        // Main thread state.
        osg::Matrixd mProjection;
        osg::Vec2f mPendingJitter;
        unsigned mJitterIndex = 0;
        bool mPreviousValid = false;
        osg::Matrixd mPreviousClip;
        osg::Matrixd mPreviousProjection;
        osg::Vec2f mPreviousJitter;
        osg::Vec3d mPreviousEye;
        int mPreviousWidth = 0;
        int mPreviousHeight = 0;

        std::array<Frame, 2> mFrames;

        // Draw thread state.
        mutable std::array<osg::ref_ptr<osg::Texture2D>, 2> mHistory;
        mutable std::array<osg::ref_ptr<osg::FrameBufferObject>, 2> mHistoryFbo;
        mutable int mHistoryWidth = 0;
        mutable int mHistoryHeight = 0;
        mutable int mWriteIndex = 0;
        mutable bool mHaveHistory = false;
        mutable unsigned mLastResolvedFrame = 0;
        mutable bool mResetRequested = false;
    };
}

#endif
