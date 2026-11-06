#include "ragdollworld.hpp"

#include <algorithm>

#include <BulletDynamics/Dynamics/btRigidBody.h>

namespace MWPhysics
{
    namespace
    {
        constexpr float sFixedStep = 1.f / 120.f;
        constexpr unsigned sMaxAwakeRigs = 12;
        constexpr unsigned sMaxRigs = 64;
    }

    RagdollWorld::RagdollWorld(btDispatcher* dispatcher, btBroadphaseInterface* broadphase,
        btConstraintSolver* solver, btCollisionConfiguration* configuration)
        : btDiscreteDynamicsWorld(dispatcher, broadphase, solver, configuration)
    {
        setGravity(btVector3(0.f, 0.f, -627.2f));
        getSolverInfo().m_numIterations = 12;
        mAwakeRigs.reserve(sMaxAwakeRigs);
    }

    bool RagdollWorld::hasCapacity() const
    {
        return mRigCount < sMaxRigs && mAwakeRigs.size() < sMaxAwakeRigs;
    }

    void RagdollWorld::addRig(Island& rig)
    {
        ++mRigCount;
        mAwakeRigs.push_back(&rig);
    }

    void RagdollWorld::removeRig(Island& rig)
    {
        --mRigCount;
        std::erase(mAwakeRigs, &rig);
    }

    bool RagdollWorld::wakeRig(Island& rig)
    {
        if (mAwakeRigs.size() >= sMaxAwakeRigs)
            return false;
        mAwakeRigs.push_back(&rig);
        return true;
    }

    void RagdollWorld::advance(float dt)
    {
        const int steps = stepSimulation(std::min(dt, .05f), 6, sFixedStep);
        if (steps == 0)
            return;

        // Predictive contacts can reference scenery that unloads between frames.
        releasePredictiveContacts();
        mStepCount += steps;
        std::erase_if(mAwakeRigs, [&](Island* rig) {
            return !rig->advance(mStepCount, static_cast<float>(steps) * sFixedStep);
        });
    }

    void RagdollWorld::updateAabbs()
    {
        // OpenMW's scheduler updates the scenery and kinematic actors.
        // Only active rigid bodies can move during a dynamics substep.
        for (int i = 0; i < m_nonStaticRigidBodies.size(); ++i)
        {
            btRigidBody* body = m_nonStaticRigidBodies[i];
            if (body->isActive())
                updateSingleAabb(body);
        }
    }

}
