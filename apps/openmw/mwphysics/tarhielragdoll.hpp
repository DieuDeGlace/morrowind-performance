#ifndef OPENMW_MWPHYSICS_TARHIELRAGDOLL_HPP
#define OPENMW_MWPHYSICS_TARHIELRAGDOLL_HPP

#include <memory>

#include <osg/Vec3f>

namespace MWRender
{
    class Animation;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWPhysics
{
    // Owns the pose handoff and simulation for the opt-in Tarhiel experiment.
    // Its lifetime follows the actor animation and ends when that model unloads.
    class TarhielRagdoll
    {
    public:
        TarhielRagdoll();
        ~TarhielRagdoll();

        static bool enabledFor(const MWWorld::Ptr& actor);
        bool update(const MWWorld::Ptr& actor, const MWRender::Animation& animation, float dt);

    private:
        class Rig;
        std::unique_ptr<Rig> mRig;
        osg::Vec3f mPreviousPosition;
        osg::Vec3f mVelocity;
        bool mHasPreviousPosition = false;
        bool mTriedActivation = false;
    };
}

#endif
