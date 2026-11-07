#ifndef OPENMW_MWPHYSICS_RAGDOLLCONTROLLERS_HPP
#define OPENMW_MWPHYSICS_RAGDOLLCONTROLLERS_HPP

#include <utility>
#include <vector>

#include <osg/Node>
#include <osgAnimation/UpdateMatrixTransform>

#include <components/nifosg/controller.hpp>
#include <components/sceneutil/keyframe.hpp>

namespace MWPhysics
{
    // Embedded animation callbacks survive Animation::resetActiveGroups().
    // Detach only transform animation; leave effects and other callbacks running.
    class RagdollControllers
    {
    public:
        RagdollControllers() = default;
        RagdollControllers(const RagdollControllers&) = delete;
        RagdollControllers& operator=(const RagdollControllers&) = delete;
        ~RagdollControllers() { restore(); }

        void suspend(osg::Node& node)
        {
            osg::ref_ptr<osg::Callback> previous;
            osg::ref_ptr<osg::Callback> callback = node.getUpdateCallback();
            while (callback)
            {
                osg::ref_ptr<osg::Callback> next = callback->getNestedCallback();
                if (dynamic_cast<SceneUtil::KeyframeController*>(callback.get())
                    || dynamic_cast<osgAnimation::UpdateMatrixTransform*>(callback.get())
                    || dynamic_cast<NifOsg::GeomMorpherController*>(callback.get()))
                {
                    mRemoved.push_back({ &node, previous, callback });
                    node.removeUpdateCallback(callback);
                    callback->setNestedCallback(nullptr);
                }
                previous = callback;
                callback = std::move(next);
            }
        }

        void restore()
        {
            // Parents in a callback chain are recorded first, so reinsertion
            // preserves the original ordering, including adjacent controllers.
            for (const Removed& removed : mRemoved)
            {
                if (removed.mPrevious)
                {
                    removed.mCallback->setNestedCallback(removed.mPrevious->getNestedCallback());
                    removed.mPrevious->setNestedCallback(removed.mCallback);
                }
                else
                {
                    removed.mCallback->setNestedCallback(removed.mNode->getUpdateCallback());
                    removed.mNode->setUpdateCallback(removed.mCallback);
                }
            }
            mRemoved.clear();
        }

    private:
        struct Removed
        {
            osg::ref_ptr<osg::Node> mNode;
            osg::ref_ptr<osg::Callback> mPrevious;
            osg::ref_ptr<osg::Callback> mCallback;
        };
        std::vector<Removed> mRemoved;
    };
}

#endif