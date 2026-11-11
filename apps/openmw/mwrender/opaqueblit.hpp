#ifndef OPENMW_MWRENDER_OPAQUEBLIT_H
#define OPENMW_MWRENDER_OPAQUEBLIT_H

#include <array>
#include <memory>

#include <osg/FrameBufferObject>
#include <osgUtil/RenderBin>

namespace Stereo
{
    class MultiviewFramebufferResolve;
}

namespace MWRender
{
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
    private:
        // GL4ES cannot blit framebuffers, so the copy is drawn instead.
        void drawCopy(osg::State& state, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf* previous,
            unsigned int frameId, osg::Texture* sceneColor, osg::Texture* sceneDepth, osg::Texture* opaqueColor);

        osg::ref_ptr<osg::Geometry> mCopyGeometry;
        osg::ref_ptr<osg::StateSet> mCopyStateSet[2];
        bool mLogged = false;
#endif
    };
}

#endif
