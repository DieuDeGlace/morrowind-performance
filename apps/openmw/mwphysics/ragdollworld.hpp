#ifndef OPENMW_MWPHYSICS_RAGDOLLWORLD_HPP
#define OPENMW_MWPHYSICS_RAGDOLLWORLD_HPP

#include <cstdint>
#include <vector>

#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

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

        bool hasCapacity() const;
        void addRig(Island& rig);
        void removeRig(Island& rig);
        bool wakeRig(Island& rig);
        bool hasRigs() const { return mRigCount != 0; }
        bool hasAwakeRigs() const { return !mAwakeRigs.empty(); }
        void advance(float dt);

    private:
        void updateAabbs() override;

        unsigned mRigCount = 0;
        std::vector<Island*> mAwakeRigs;
        std::uint64_t mStepCount = 0;
    };
}

#endif
