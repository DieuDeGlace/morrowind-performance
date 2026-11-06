#include "ragdollworld.hpp"

#include <algorithm>

#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <components/debug/debuglog.hpp>

#include "collisiontype.hpp"

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

    RagdollWorld::~RagdollWorld()
    {
        releaseGrab();
    }

    void RagdollWorld::grab(Island& rig, btRigidBody& part, const btVector3& point, const btVector3& eye,
        const btQuaternion& view, btScalar impulseLimit)
    {
        releaseGrab();
        const btVector3 localPoint = part.getWorldTransform().inverse() * point;
        mGrab = std::make_unique<btPoint2PointConstraint>(part, localPoint);
        mGrab->m_setting.m_tau = btScalar(.25);
        mGrab->m_setting.m_impulseClamp = impulseLimit;
        mGrabViewOffset = quatRotate(view.inverse(), point - eye);
        mGrabStart = point;
        mGrabbedRig = &rig;
        part.forceActivationState(DISABLE_DEACTIVATION);
        addConstraint(mGrab.get());
    }

    void RagdollWorld::moveGrab(const btVector3& eye, const btQuaternion& view, btScalar reach, float dt)
    {
        const auto& part = mGrab->getRigidBodyA();
        const btVector3 contact = part.getWorldTransform() * mGrab->getPivotInA();
        const btScalar maxStretch = reach * btScalar(1.5);
        if ((contact - eye).length2() > maxStretch * maxStretch)
        {
            releaseGrab();
            return;
        }

        btVector3 target = clipGrabTarget(eye, eye + quatRotate(view, mGrabViewOffset));
        const btVector3 previous = mGrab->getPivotInB();
        const btVector3 delta = target - previous;
        const btScalar maxTravel = 400 * std::clamp(dt, 0.f, .05f);
        const btScalar distance = delta.length();
        if (distance > maxTravel)
            target = previous + delta * (maxTravel / distance);
        mGrab->setPivotB(target);
    }

    btVector3 RagdollWorld::clipGrabTarget(const btVector3& eye, const btVector3& target) const
    {
        const btVector3 offset = target - eye;
        const btScalar distance = offset.length();
        if (distance < 1)
            return target;

        btCollisionWorld::ClosestRayResultCallback obstruction(eye, target);
        obstruction.m_collisionFilterGroup = CollisionType_Actor;
        obstruction.m_collisionFilterMask = CollisionType_RagdollScenery;
        rayTest(eye, target, obstruction);
        if (!obstruction.hasHit())
            return target;

        constexpr btScalar clearance = 5;
        const btScalar fraction = btMax(btScalar(0), obstruction.m_closestHitFraction - clearance / distance);
        return eye + offset * fraction;
    }

    void RagdollWorld::releaseGrab()
    {
        if (!mGrab)
            return;

        btRigidBody& part = mGrab->getRigidBodyA();
        const btVector3 contact = part.getWorldTransform() * mGrab->getPivotInA();
        removeConstraint(mGrab.get());
        mGrab.reset();
        mGrabbedRig = nullptr;
        part.forceActivationState(ACTIVE_TAG);
        part.setDeactivationTime(0);
        Log(Debug::Info) << "Ragdoll grab: released after moving " << (contact - mGrabStart).length() << " units";
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
        if (isGrabbed(rig))
            releaseGrab();
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
