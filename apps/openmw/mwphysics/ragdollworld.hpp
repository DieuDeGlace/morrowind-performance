#ifndef OPENMW_MWPHYSICS_RAGDOLLWORLD_HPP
#define OPENMW_MWPHYSICS_RAGDOLLWORLD_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

class btPoint2PointConstraint;

namespace MWPhysics
{
    // Uses the game's static colliders and broadphase. Ragdoll bodies have
    // their own filter bit and stay out of the ordinary actor queries.
    class RagdollWorld final : public btDiscreteDynamicsWorld
    {
    public:
        class Island
        {
        public:
            virtual ~Island() = default;
            virtual bool advance(std::uint64_t step, float elapsed) = 0;
        };

        RagdollWorld(btDispatcher* dispatcher, btBroadphaseInterface* broadphase,
            btConstraintSolver* solver, btCollisionConfiguration* configuration);
        ~RagdollWorld() override;

        bool hasGrab() const { return mGrab != nullptr; }
        bool isGrabbed(const Island& rig) const { return mGrabbedRig == &rig; }
        void grab(Island& rig, btRigidBody& part, const btVector3& point, const btVector3& eye,
            const btQuaternion& view, btScalar impulseLimit);
        void moveGrab(const btVector3& eye, const btQuaternion& view, btScalar reach, float dt);
        void releaseGrab();

        bool hasCapacity() const;
        void addRig(Island& rig);
        void removeRig(Island& rig);
        bool wakeRig(Island& rig);
        bool hasRigs() const { return mRigCount != 0; }
        bool hasAwakeRigs() const { return !mAwakeRigs.empty(); }
        void advance(float dt);

    private:
        void updateAabbs() override;

        std::unique_ptr<btPoint2PointConstraint> mGrab;
        Island* mGrabbedRig = nullptr;
        btVector3 mGrabViewOffset;
        btVector3 mGrabStart;
        unsigned mRigCount = 0;
        std::vector<Island*> mAwakeRigs;
        std::uint64_t mStepCount = 0;
    };
}

#endif
