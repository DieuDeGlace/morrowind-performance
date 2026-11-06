#include "tarhielragdoll.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <btBulletCollisionCommon.h>

#include <osg/MatrixTransform>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwrender/animation.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"

namespace MWPhysics
{
    namespace
    {
        // The Bip01 bone names and anatomical ordering follow the skeleton
        // mapping in lihogloglo/openmw-dev's experimental physics branch.
        enum PartIndex
        {
            Pelvis,
            Chest,
            Head,
            LeftUpperArm,
            LeftForearm,
            RightUpperArm,
            RightForearm,
            LeftThigh,
            LeftCalf,
            RightThigh,
            RightCalf,
            PartCount
        };

        struct PartSpec
        {
            std::string_view mBone;
            std::string_view mTip;
            PartIndex mParent;
            float mRadius;
            float mMass;
            float mSwing;
            float mSideSwing;
            float mTwist;
        };

        constexpr std::array<PartSpec, PartCount> sParts{ {
            { "Bip01 Pelvis", "", Pelvis, 9.f, 12.f, 0.f, 0.f, 0.f },
            { "Bip01 Spine", "Bip01 Neck", Pelvis, 9.f, 18.f, 0.5f, 0.35f, 0.25f },
            { "Bip01 Head", "", Chest, 7.f, 5.f, 0.6f, 0.45f, 0.4f },
            { "Bip01 L UpperArm", "Bip01 L Forearm", Chest, 4.f, 3.f, 1.25f, 1.0f, 0.65f },
            { "Bip01 L Forearm", "Bip01 L Hand", LeftUpperArm, 3.5f, 2.f, 1.65f, 0.2f, 0.2f },
            { "Bip01 R UpperArm", "Bip01 R Forearm", Chest, 4.f, 3.f, 1.25f, 1.0f, 0.65f },
            { "Bip01 R Forearm", "Bip01 R Hand", RightUpperArm, 3.5f, 2.f, 1.65f, 0.2f, 0.2f },
            { "Bip01 L Thigh", "Bip01 L Calf", Pelvis, 5.5f, 8.f, 0.9f, 0.55f, 0.35f },
            { "Bip01 L Calf", "Bip01 L Foot", LeftThigh, 4.f, 4.f, 1.5f, 0.16f, 0.12f },
            { "Bip01 R Thigh", "Bip01 R Calf", Pelvis, 5.5f, 8.f, 0.9f, 0.55f, 0.35f },
            { "Bip01 R Calf", "Bip01 R Foot", RightThigh, 4.f, 4.f, 1.5f, 0.16f, 0.12f },
        } };

        constexpr float sFixedStep = 1.f / 120.f;
        constexpr float sMaxFrameTime = 0.05f;
        constexpr float sMaxInheritedSpeed = 240.f;
        constexpr int sTerrainRadius = 10;
        constexpr int sTerrainSide = 2 * sTerrainRadius + 1;
        constexpr float sTerrainSpacing = 32.f;

        osg::Matrixf toMatrix(const btTransform& transform)
        {
            const auto rotation = Misc::Convert::toOsg(transform.getRotation());
            const auto position = Misc::Convert::toOsg(transform.getOrigin());
            return osg::Matrixf::rotate(rotation) * osg::Matrixf::translate(position);
        }

        osg::Vec3f limitVelocity(osg::Vec3f velocity)
        {
            const float speed = velocity.length();
            if (speed > sMaxInheritedSpeed)
                velocity *= sMaxInheritedSpeed / speed;
            return velocity;
        }
    }

    class TarhielRagdoll::Rig
    {
    public:
        Rig()
            : mDispatcher(&mConfiguration)
            , mWorld(&mDispatcher, &mBroadphase, &mSolver, &mConfiguration)
        {
            mWorld.setGravity(btVector3(0.f, 0.f, -627.2f));
            mWorld.getSolverInfo().m_numIterations = 18;
        }

        ~Rig()
        {
            for (const auto& joint : mJoints)
                mWorld.removeConstraint(joint.get());
            for (const auto& part : mParts)
                if (part.mBody)
                    mWorld.removeRigidBody(part.mBody.get());
            if (mGround)
                mWorld.removeRigidBody(mGround.get());
        }

        bool initialize(const MWWorld::Ptr& actor, const MWRender::Animation& animation, const osg::Vec3f& velocity)
        {
            if (!actor.isInCell() || !actor.getCell()->isExterior())
                return false;

            // Resolve all bones before changing the actor's animation or pose.
            for (size_t i = 0; i < mParts.size(); ++i)
            {
                Part& part = mParts[i];
                part.mNode
                    = dynamic_cast<osg::MatrixTransform*>(const_cast<osg::Node*>(animation.getNode(sParts[i].mBone)));
                if (part.mNode == nullptr)
                {
                    Log(Debug::Warning) << "Tarhiel ragdoll: missing bone " << sParts[i].mBone;
                    return false;
                }
                const auto paths = part.mNode->getParentalNodePaths();
                if (paths.size() != 1 || paths.front().size() < 2)
                    return false;

                part.mParentPath = paths.front();
                part.mParentPath.pop_back();
                part.mOriginalLocal = part.mNode->getMatrix();
                part.mInitialWorld = part.mOriginalLocal * osg::computeLocalToWorld(part.mParentPath);
                mPoseOrder[i] = i;
            }
            std::ranges::sort(mPoseOrder, {}, [&](size_t index) { return mParts[index].mParentPath.size(); });

            const auto& world = *MWBase::Environment::get().getWorld();
            const auto worldspace = actor.getCell()->getCell()->getWorldSpace();
            const osg::Vec3f center = mParts[Pelvis].mInitialWorld.getTrans();
            if (!createTerrain(world, worldspace, center))
                return false;

            // Lift the initial capsules out of shallow overlaps at the handoff.
            // The visual pose and all joint anchors are translated by the same amount.
            float lift = 0.f;
            for (size_t i = 0; i < mParts.size(); ++i)
            {
                const auto& spec = sParts[i];
                Part& part = mParts[i];
                const float radius = spec.mRadius * static_cast<float>(part.mInitialWorld.getScale().x());
                const osg::Vec3f pivot = part.mInitialWorld.getTrans();
                const auto tip = spec.mTip.empty() ? std::nullopt : animation.getNodeWorldTransform(spec.mTip);
                if (!spec.mTip.empty() && !tip)
                    return false;

                const osg::Vec3f end = tip ? osg::Vec3f(tip->getTrans()) : pivot;
                const osg::Vec3f segment = end - pivot;
                const float length = segment.length();
                const float cylinder = std::max(0.f, length - 2.f * radius);
                const osg::Vec3f midpoint = (pivot + end) * 0.5f;
                btQuaternion rotation = btQuaternion::getIdentity();
                if (cylinder > 0.f)
                {
                    rotation = shortestArcQuat(btVector3(0.f, 1.f, 0.f), Misc::Convert::toBullet(segment / length));
                    part.mShape = std::make_unique<btCapsuleShape>(radius, cylinder);
                }
                else
                    part.mShape = std::make_unique<btSphereShape>(radius);

                part.mInitialBody = btTransform(rotation, Misc::Convert::toBullet(midpoint));
                part.mBoneToBody = part.mInitialWorld * osg::Matrixf::inverse(toMatrix(part.mInitialBody));
                const float verticalExtent
                    = radius + (length > 0.f ? cylinder * std::abs(segment.z()) / (2.f * length) : 0.f);
                lift = std::max(lift, world.getTerrainHeightAt(midpoint, worldspace) + verticalExtent - midpoint.z());
            }

            mJoints.reserve(PartCount - 1);
            const btVector3 offset(0.f, 0.f, lift + 1.f);
            for (size_t i = 0; i < mParts.size(); ++i)
            {
                Part& part = mParts[i];
                const auto& spec = sParts[i];
                btVector3 inertia;
                part.mShape->calculateLocalInertia(spec.mMass, inertia);
                btRigidBody::btRigidBodyConstructionInfo info(spec.mMass, nullptr, part.mShape.get(), inertia);
                info.m_startWorldTransform = part.mInitialBody;
                info.m_startWorldTransform.getOrigin() += offset;
                info.m_friction = 0.85f;
                info.m_linearDamping = 0.18f;
                info.m_angularDamping = 0.65f;
                part.mBody = std::make_unique<btRigidBody>(info);
                part.mBody->setSleepingThresholds(6.f, 0.25f);
                part.mBody->setCcdMotionThreshold(2.f);
                part.mBody->setCcdSweptSphereRadius(spec.mRadius * 0.55f);
                part.mBody->setLinearVelocity(Misc::Convert::toBullet(velocity));
                mWorld.addRigidBody(part.mBody.get());

                if (i == Pelvis)
                    continue;

                auto& parent = *mParts[spec.mParent].mBody;
                const auto& childTransform = part.mBody->getWorldTransform();
                const auto& parentTransform = parent.getWorldTransform();
                btTransform anchor = childTransform;
                // Bullet's twist axis is X. The capsules were built along Y.
                anchor.setRotation(childTransform.getRotation() * btQuaternion(btVector3(0, 0, 1), SIMD_HALF_PI));
                anchor.setOrigin(Misc::Convert::toBullet(part.mInitialWorld.getTrans()) + offset);
                auto joint = std::make_unique<btConeTwistConstraint>(
                    parent, *part.mBody, parentTransform.inverse() * anchor, childTransform.inverse() * anchor);
                joint->setLimit(spec.mSwing, spec.mSideSwing, spec.mTwist);
                joint->setDamping(0.2f);
                mWorld.addConstraint(joint.get(), true);
                mJoints.push_back(std::move(joint));
                part.mInitialJointRotation = parentTransform.getRotation().inverse() * childTransform.getRotation();
            }

            Log(Debug::Info) << "Tarhiel ragdoll: activated " << mParts.size() << " bodies and " << mJoints.size()
                             << " joints";
            return true;
        }

        void advance(float dt)
        {
            if (mSettled || dt <= 0.f)
                return;

            if (mWorld.stepSimulation(std::min(dt, sMaxFrameTime), 6, sFixedStep) == 0)
                return;
            for (size_t index : mPoseOrder)
            {
                const Part& part = mParts[index];
                const osg::Matrixf boneWorld = part.mBoneToBody * toMatrix(part.mBody->getWorldTransform());
                const osg::Matrixf parentWorld = osg::computeLocalToWorld(part.mParentPath);
                part.mNode->setMatrix(boneWorld * osg::Matrixf::inverse(parentWorld));
            }
            reportArticulation();

            mSettled = std::ranges::none_of(mParts, [](const Part& part) { return part.mBody->isActive(); });
            if (mSettled)
                Log(Debug::Info) << "Tarhiel ragdoll: settled";
        }

        void restorePose() const
        {
            for (const Part& part : mParts)
                part.mNode->setMatrix(part.mOriginalLocal);
        }

    private:
        struct Part
        {
            osg::MatrixTransform* mNode = nullptr;
            osg::NodePath mParentPath;
            osg::Matrixf mOriginalLocal;
            osg::Matrixf mInitialWorld;
            osg::Matrixf mBoneToBody;
            btTransform mInitialBody;
            btQuaternion mInitialJointRotation;
            std::unique_ptr<btCollisionShape> mShape;
            std::unique_ptr<btRigidBody> mBody;
        };

        bool createTerrain(const MWBase::World& world, ESM::RefId worldspace, const osg::Vec3f& center)
        {
            // A small snapshot of the exterior terrain is enough for the falling
            // wizard test. The main Bullet collision world stays on its usual path.
            std::array<btVector3, sTerrainSide * sTerrainSide> vertices;
            for (int y = 0; y < sTerrainSide; ++y)
            {
                for (int x = 0; x < sTerrainSide; ++x)
                {
                    osg::Vec3f point(center.x() + (x - sTerrainRadius) * sTerrainSpacing,
                        center.y() + (y - sTerrainRadius) * sTerrainSpacing, center.z());
                    point.z() = world.getTerrainHeightAt(point, worldspace);
                    if (!std::isfinite(point.z()) || std::abs(point.z() - center.z()) > 8192.f)
                        return false;
                    vertices[y * sTerrainSide + x] = Misc::Convert::toBullet(point);
                }
            }

            for (int y = 0; y < sTerrainSide - 1; ++y)
            {
                for (int x = 0; x < sTerrainSide - 1; ++x)
                {
                    const int i = y * sTerrainSide + x;
                    mTerrain.addTriangle(vertices[i], vertices[i + 1], vertices[i + sTerrainSide]);
                    mTerrain.addTriangle(vertices[i + 1], vertices[i + sTerrainSide + 1], vertices[i + sTerrainSide]);
                }
            }
            mGroundShape = std::make_unique<btBvhTriangleMeshShape>(&mTerrain, true);
            btRigidBody::btRigidBodyConstructionInfo info(0.f, nullptr, mGroundShape.get(), btVector3(0, 0, 0));
            info.m_friction = 1.f;
            mGround = std::make_unique<btRigidBody>(info);
            mWorld.addRigidBody(mGround.get());
            return true;
        }

        float jointMotion(PartIndex index) const
        {
            const Part& child = mParts[index];
            const Part& parent = mParts[sParts[index].mParent];
            const btQuaternion relative = parent.mBody->getWorldTransform().getRotation().inverse()
                * child.mBody->getWorldTransform().getRotation();
            return (child.mInitialJointRotation.inverse() * relative).getAngleShortestPath();
        }

        void reportArticulation()
        {
            if (mReportedArticulation)
                return;
            mElbowMotion = std::max({ mElbowMotion, jointMotion(LeftForearm), jointMotion(RightForearm) });
            mKneeMotion = std::max({ mKneeMotion, jointMotion(LeftCalf), jointMotion(RightCalf) });
            if (mElbowMotion > 0.1f && mKneeMotion > 0.1f)
            {
                Log(Debug::Info) << "Tarhiel ragdoll: independent elbow and knee motion (radians " << mElbowMotion
                                 << ", " << mKneeMotion << ")";
                mReportedArticulation = true;
            }
        }

        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher;
        btDbvtBroadphase mBroadphase;
        btSequentialImpulseConstraintSolver mSolver;
        btDiscreteDynamicsWorld mWorld;
        btTriangleMesh mTerrain;
        std::unique_ptr<btBvhTriangleMeshShape> mGroundShape;
        std::unique_ptr<btRigidBody> mGround;
        std::array<Part, PartCount> mParts;
        std::array<size_t, PartCount> mPoseOrder{};
        std::vector<std::unique_ptr<btConeTwistConstraint>> mJoints;
        float mElbowMotion = 0.f;
        float mKneeMotion = 0.f;
        bool mReportedArticulation = false;
        bool mSettled = false;
    };

    TarhielRagdoll::TarhielRagdoll() = default;
    TarhielRagdoll::~TarhielRagdoll() = default;

    bool TarhielRagdoll::enabledFor(const MWWorld::Ptr& actor)
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_TARHIEL_RAGDOLL");
            return value && std::string_view(value) == "1";
        }();
        static const ESM::RefId tarhiel = ESM::RefId::stringRefId("agronian guy");
        return enabled && actor.getCellRef().getRefId() == tarhiel;
    }

    bool TarhielRagdoll::active() const
    {
        return mRig != nullptr;
    }

    bool TarhielRagdoll::update(const MWWorld::Ptr& actor, const MWRender::Animation& animation, float dt)
    {
        if (!actor.getClass().getCreatureStats(actor).isDead())
        {
            if (mRig)
            {
                mRig->restorePose();
                mRig.reset();
            }
            mTriedActivation = false;
            if (dt > 0.f)
            {
                const osg::Vec3f position = actor.getRefData().getPosition().asVec3();
                const osg::Vec3f delta = position - mPreviousPosition;
                if (mHasPreviousPosition && delta.length2() < 256.f * 256.f)
                    mVelocity = limitVelocity(delta / dt);
                else
                    mVelocity.set(0.f, 0.f, 0.f);
                mPreviousPosition = position;
                mHasPreviousPosition = true;
            }
            return false;
        }

        if (!mTriedActivation && dt > 0.f)
        {
            mTriedActivation = true;
            auto rig = std::make_unique<Rig>();
            if (rig->initialize(actor, animation, mVelocity))
                mRig = std::move(rig);
            else
                Log(Debug::Warning) << "Tarhiel ragdoll: native death animation retained";
        }
        if (!mRig)
            return false;

        mRig->advance(dt);
        return true;
    }
}
