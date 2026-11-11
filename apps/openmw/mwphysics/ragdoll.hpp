#ifndef OPENMW_MWPHYSICS_RAGDOLL_HPP
#define OPENMW_MWPHYSICS_RAGDOLL_HPP

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
    class PhysicsSystem;

    class Ragdoll
    {
    public:
        static bool enabled();
        static std::unique_ptr<Ragdoll> create(
            const MWWorld::Ptr& actor, const MWRender::Animation& animation, const osg::Vec3f& velocity);
        static void hit(const MWWorld::Ptr& player, float reach, float attackStrength);
        static bool canGrab(const MWWorld::Ptr& corpse);
        static void grab(const MWWorld::Ptr& player, const MWWorld::Ptr& corpse, const osg::Vec3f& point);
        static void moveGrab(const MWWorld::Ptr& player, float dt);
        static void releaseGrab();
        static bool throwGrabbedItem(const MWWorld::Ptr& player);

        ~Ragdoll();
        void updatePose();
        void restorePose() const;

    private:
        class Rig;
        explicit Ragdoll(std::unique_ptr<Rig> rig);

        std::unique_ptr<Rig> mRig;
    };
}

#endif
