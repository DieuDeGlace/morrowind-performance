#include "ragdoll.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <osg/MatrixTransform>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwrender/actoranimation.hpp"
#include "../mwrender/camera.hpp"
#include "../mwworld/class.hpp"

#include "physicssystem.hpp"
#include "ragdollautorig.hpp"
#include "ragdollcontrollers.hpp"
#include "ragdollworld.hpp"

namespace MWPhysics
{
    namespace
    {
        struct PartSpec
        {
            std::string_view mBone;
            std::string_view mTip;
            std::size_t mParent;
            float mRadius;
            float mMass;
            float mSwing;
            float mSideSwing;
            float mTwist;
        };

        enum BipedPart : std::size_t
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
        };

        // Parents appear before their children. Radii and masses describe the
        // reference rig; segment endpoints come from the actor's actual pose.
        constexpr PartSpec sBiped[] = {
            { "Bip01 Pelvis", "", Pelvis, 9.f, 12.f, 0.f, 0.f, 0.f },
            { "Bip01 Spine", "Bip01 Neck", Pelvis, 9.f, 18.f, .5f, .35f, .25f },
            { "Bip01 Head", "", Chest, 7.f, 5.f, .6f, .45f, .4f },
            { "Bip01 L UpperArm", "Bip01 L Forearm", Chest, 4.f, 3.f, 1.25f, 1.f, .65f },
            { "Bip01 L Forearm", "Bip01 L Hand", LeftUpperArm, 3.5f, 2.f, 1.65f, .2f, .2f },
            { "Bip01 R UpperArm", "Bip01 R Forearm", Chest, 4.f, 3.f, 1.25f, 1.f, .65f },
            { "Bip01 R Forearm", "Bip01 R Hand", RightUpperArm, 3.5f, 2.f, 1.65f, .2f, .2f },
            { "Bip01 L Thigh", "Bip01 L Calf", Pelvis, 5.5f, 8.f, .9f, .55f, .35f },
            { "Bip01 L Calf", "Bip01 L Foot", LeftThigh, 4.f, 4.f, 1.5f, .16f, .12f },
            { "Bip01 R Thigh", "Bip01 R Calf", Pelvis, 5.5f, 8.f, .9f, .55f, .35f },
            { "Bip01 R Calf", "Bip01 R Foot", RightThigh, 4.f, 4.f, 1.5f, .16f, .12f },
        };
        constexpr PartSpec sTail[] = {
            { "Bip01 Tail", "Bip01 Tail1", Pelvis, 3.f, 2.f, .7f, .65f, .3f },
            { "Bip01 Tail1", "Bip01 Tail2", std::size(sBiped), 2.f, 1.f, .7f, .65f, .3f },
        };

        constexpr PartSpec sCliffRacer[] = {
            { "Body Bone", "Neck", 0, 9.f, 12.f, 0.f, 0.f, 0.f },
            { "Neck", "Head", 0, 5.f, 3.f, .7f, .5f, .35f },
            { "Head", "", 1, 6.f, 3.f, .55f, .5f, .3f },
            { "L Wing", "L Wing_1", 0, 5.f, 4.f, 1.2f, .6f, .45f },
            { "L Wing_1", "L Wing_3", 3, 3.f, 2.f, 1.25f, .3f, .25f },
            { "R Wing", "R Wing_1", 0, 5.f, 4.f, 1.2f, .6f, .45f },
            { "R Wing_1", "R Wing_4", 5, 3.f, 2.f, 1.25f, .3f, .25f },
            { "TailRoot", "Tail_2", 0, 4.f, 2.f, .8f, .65f, .3f },
            { "Tail_2", "Tail_4", 7, 2.5f, 1.f, .8f, .65f, .3f },
        };

        constexpr PartSpec sShalk[] = {
            { "Body Bone", "", 0, 14.f, 16.f, 0.f, 0.f, 0.f },
            { "head", "", 0, 7.f, 4.f, .6f, .5f, .35f },
            { "L1_digit00", "L1_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "L1_digit01", "L1_digit02", 2, 3.f, 1.f, 1.1f, .2f, .2f },
            { "L2_digit00", "L2_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "L2_digit01", "L2_digit02", 4, 3.f, 1.f, 1.1f, .2f, .2f },
            { "L3_digit00", "L3_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "L3_digit01", "L3_digit02", 6, 3.f, 1.f, 1.1f, .2f, .2f },
            { "R1_digit00", "R1_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "R1_digit01", "R1_digit02", 8, 3.f, 1.f, 1.1f, .2f, .2f },
            { "R2_digit00", "R2_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "R2_digit01", "R2_digit02", 10, 3.f, 1.f, 1.1f, .2f, .2f },
            { "R3_digit00", "R3_digit01", 0, 4.f, 2.f, .9f, .5f, .3f },
            { "R3_digit01", "R3_digit02", 12, 3.f, 1.f, 1.1f, .2f, .2f },
        };

        constexpr PartSpec sMudcrab[] = {
            { "Root Bone", "Bone02", 0, 22.f, 16.f, 0.f, 0.f, 0.f },
            { "Bone27", "Bone06", 0, 6.f, 3.f, .9f, .5f, .3f },
            { "Bone06", "Bone07", 1, 6.f, 2.f, 1.1f, .25f, .25f },
            { "Bone26", "Bone04", 0, 6.f, 3.f, .9f, .5f, .3f },
            { "Bone04", "Bone05", 3, 6.f, 2.f, 1.1f, .25f, .25f },
            { "Bone08", "Bone09", 0, 4.f, 2.f, .8f, .5f, .3f },
            { "Bone09", "Bone10", 5, 3.f, 1.f, 1.1f, .25f, .2f },
            { "Bone11", "Bone12", 0, 4.f, 2.f, .8f, .5f, .3f },
            { "Bone12", "Bone25", 7, 3.f, 1.f, 1.1f, .25f, .2f },
            { "Bone13", "Bone14", 0, 4.f, 2.f, .8f, .5f, .3f },
            { "Bone14", "Bone15", 9, 3.f, 1.f, 1.1f, .25f, .2f },
            { "Bone16", "Bone17", 0, 4.f, 2.f, .8f, .5f, .3f },
            { "Bone17", "Bone18", 11, 3.f, 1.f, 1.1f, .25f, .2f },
            { "Bone19", "Bone20", 0, 3.f, 1.f, .8f, .5f, .3f },
            { "Bone20", "Bone21", 13, 2.f, 1.f, 1.1f, .25f, .2f },
            { "Bone22", "Bone23", 0, 3.f, 1.f, .8f, .5f, .3f },
            { "Bone23", "Bone24", 15, 2.f, 1.f, 1.1f, .25f, .2f },
        };

        struct PartHit
        {
            btRigidBody* mBody = nullptr;
            btVector3 mPoint;
        };

        PartHit findPart(RagdollWorld& world, const btVector3& from, const btVector3& to, btScalar radius)
        {
            const btSphereShape sweep(radius);
            const auto rotation = btQuaternion::getIdentity();
            btCollisionWorld::ClosestConvexResultCallback hit(from, to);
            hit.m_collisionFilterGroup = CollisionType_Actor | CollisionType_World;
            hit.m_collisionFilterMask = CollisionType_RagdollScenery | CollisionType_Ragdoll;
            world.convexSweepTest(&sweep, btTransform(rotation, from), btTransform(rotation, to), hit);
            if (!hit.hasHit()
                || hit.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup != CollisionType_Ragdoll)
                return {};
            return { btRigidBody::upcast(const_cast<btCollisionObject*>(hit.m_hitCollisionObject)),
                hit.m_hitPointWorld };
        }

        Ragdoll* grabbableCorpse(const MWWorld::Ptr& corpse)
        {
            if (!Ragdoll::enabled() || corpse.isEmpty() || !corpse.getClass().isActor()
                || !corpse.getClass().getCreatureStats(corpse).isDead())
                return nullptr;
            const auto* animation = dynamic_cast<const MWRender::ActorAnimation*>(
                MWBase::Environment::get().getWorld()->getAnimation(corpse));
            return animation ? animation->getRagdoll() : nullptr;
        }

        osg::Vec3f playerEye(MWBase::World& world, const MWWorld::Ptr& player)
        {
            return player.getRefData().getPosition().asVec3()
                + osg::Vec3f(0.f, 0.f, 1.7f * world.getHalfExtents(player, true).z());
        }

        osg::Vec3f grabEye(MWBase::World& world, const MWWorld::Ptr& player, const MWRender::Camera& camera)
        {
            if (camera.getMode() == MWRender::Camera::Mode::FirstPerson)
                return osg::Vec3f(camera.getPosition());
            return playerEye(world, player);
        }

        osg::Matrixf toMatrix(const btTransform& transform)
        {
            return osg::Matrixf::rotate(Misc::Convert::toOsg(transform.getRotation()))
                * osg::Matrixf::translate(Misc::Convert::toOsg(transform.getOrigin()));
        }

        osg::NodePath actorPath(const osg::Node& node, const osg::Node& root)
        {
            auto paths = node.getParentalNodePaths(const_cast<osg::Node*>(&root));
            if (paths.size() != 1 || paths.front().size() < 2 || paths.front().front() != &root)
                return {};
            return std::move(paths.front());
        }
    }

    class Ragdoll::Rig final : public RagdollWorld::Island
    {
    public:
        Rig(PhysicsSystem& physics, const MWWorld::Ptr& actor)
            : mPhysics(physics)
            , mActorId(actor.getCellRef().getRefId())
            , mRoot(actor.getRefData().getBaseNode())
        {
        }

        ~Rig() override
        {
            if (!mRegistered)
                return;
            mPhysics.withRagdollWorld([this](RagdollWorld& world) {
                world.removeRig(*this);
                for (const auto& joint : mJoints)
                    world.removeConstraint(joint.get());
                for (const auto& part : mParts)
                    world.removeRigidBody(part.mBody.get());
            });
        }

        bool initialize(const MWRender::Animation& animation, const osg::Vec3f& velocity)
        {
            if (!mRoot)
                return false;

            // Creature variants often share the same skeleton, regardless of
            // their record ID or ESM. BBip01 is used by the kwama worker.
            std::span<const PartSpec> specs;
            std::string_view profile;
            std::string_view prefix;
            const bool standardBiped = animation.getNode(sBiped[Pelvis].mBone) != nullptr;
            if (standardBiped || animation.getNode("BBip01 Pelvis"))
            {
                specs = sBiped;
                profile = "Bip01";
                if (!standardBiped)
                    prefix = "B";
            }
            else if (animation.getNode("L Wing") && animation.getNode("TailRoot"))
            {
                specs = sCliffRacer;
                profile = "cliff racer";
            }
            else if (animation.getNode("L1_digit00") && animation.getNode("R3_digit02"))
            {
                specs = sShalk;
                profile = "shalk";
            }
            else if (animation.getNode("Bone27") && animation.getNode("Bone26") && animation.getNode("Bone29"))
            {
                specs = sMudcrab;
                profile = "mudcrab";
            }
            const auto findBone = [&](std::string_view name) {
                return animation.getNode(prefix.empty() ? name : std::string(prefix) + std::string(name));
            };

            std::vector<PartSpec> parts(specs.begin(), specs.end());
            if (profile == "Bip01")
            {
                for (const auto& tail : sTail)
                {
                    if (!findBone(tail.mBone) || !findBone(tail.mTip))
                        break;
                    parts.push_back(tail);
                }
            }

            // A partial/custom Bip01 rig must use automatic discovery too,
            // rather than failing because just one template bone is missing.
            if (!std::ranges::all_of(parts, [&](const PartSpec& part) {
                    return dynamic_cast<const osg::MatrixTransform*>(findBone(part.mBone))
                        && (part.mTip.empty() || findBone(part.mTip));
                }))
                parts.clear();

            const auto appendPart = [&](osg::MatrixTransform* node, const osg::Vec3f* start, const osg::Vec3f* end) {
                Part part;
                part.mNode = node;
                part.mParentPath = actorPath(*node, *mRoot);
                if (part.mParentPath.empty())
                    return false;
                part.mParentPath.pop_back();
                part.mOriginalLocal = node->getMatrix();
                part.mInitialWorld = part.mOriginalLocal * osg::computeLocalToWorld(part.mParentPath);
                part.mStart = start ? *start : part.mInitialWorld.getTrans();
                part.mEnd = end ? *end : part.mStart;
                mPoseOrder.push_back(mParts.size());
                mParts.push_back(std::move(part));
                return true;
            };

            float size = 1.f;
            if (parts.empty())
            {
                const auto* model = animation.getObjectRoot();
                if (!model)
                    return false;
                mModelFrame = std::make_unique<RagdollModelFrame>(*const_cast<osg::Group*>(model));
                auto* frame = mModelFrame->get();
                if (!frame)
                    return false;
                const auto path = actorPath(*frame, *mRoot);
                if (path.empty())
                    return false;
                AutoRagdoll generated = makeAutoRagdoll(*frame, osg::computeLocalToWorld(path));
                if (generated.mParts.empty())
                {
                    Log(Debug::Warning) << "Ragdoll: " << mActorId << " has no usable model geometry";
                    return false;
                }
                profile = generated.mParts.size() > 1 ? "automatic skeleton" : "whole model (no usable joints)";
                for (const auto& part : generated.mParts)
                {
                    if (!appendPart(part.mNode, &part.mStart, &part.mEnd))
                        return false;
                    parts.push_back({ "", "", part.mParent, part.mRadius, part.mMass, 1.1f, .85f, .45f });
                }
                mAnimatedNodes = std::move(generated.mAnimatedNodes);
            }
            else
            {
                mParts.reserve(parts.size());
                for (const PartSpec& spec : parts)
                {
                    osg::Vec3f end;
                    const osg::Vec3f* endpoint = nullptr;
                    if (!spec.mTip.empty())
                    {
                        const auto path = actorPath(*findBone(spec.mTip), *mRoot);
                        if (path.empty())
                            return false;
                        end = osg::computeLocalToWorld(path).getTrans();
                        endpoint = &end;
                    }
                    auto* node = dynamic_cast<osg::MatrixTransform*>(const_cast<osg::Node*>(findBone(spec.mBone)));
                    if (!appendPart(node, nullptr, endpoint))
                        return false;
                }

                // Scale known templates using actual torso and leg lengths.
                size = static_cast<float>(mParts[0].mInitialWorld.getScale().x());
                if (profile == "Bip01")
                {
                    const auto length
                        = [&](std::size_t index) { return (mParts[index].mEnd - mParts[index].mStart).length(); };
                    size = (length(Chest) + length(LeftThigh) + length(LeftCalf)) / 88.f;
                }
                size = std::clamp(size, .2f, 4.f);
            }
            std::ranges::sort(mPoseOrder, {}, [&](std::size_t index) { return mParts[index].mParentPath.size(); });

            mJoints.reserve(mParts.size() - 1);
            for (std::size_t i = 0; i < mParts.size(); ++i)
            {
                const PartSpec& spec = parts[i];
                Part& part = mParts[i];
                const float radius = spec.mRadius * size;
                const osg::Vec3f pivot = part.mInitialWorld.getTrans();
                const osg::Vec3f segment = part.mEnd - part.mStart;
                const float length = segment.length();
                const float cylinder = std::max(0.f, length - 2.f * radius);
                part.mRadius = radius;
                part.mHalfLength = cylinder * .5f;
                btQuaternion rotation = btQuaternion::getIdentity();
                if (cylinder > 0.f)
                {
                    rotation = shortestArcQuat(btVector3(0.f, 1.f, 0.f), Misc::Convert::toBullet(segment / length));
                    part.mShape = std::make_unique<btCapsuleShape>(radius, cylinder);
                }
                else
                    part.mShape = std::make_unique<btSphereShape>(radius);

                const btVector3 center = Misc::Convert::toBullet(part.mStart + segment * .5f);
                const btTransform initial(rotation, center);
                part.mBoneToBody = part.mInitialWorld * osg::Matrixf::inverse(toMatrix(initial));
                const float mass = spec.mMass * size * size;
                mMass += mass;
                btVector3 inertia;
                part.mShape->calculateLocalInertia(mass, inertia);
                btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr, part.mShape.get(), inertia);
                info.m_startWorldTransform = initial;
                info.m_startWorldTransform.getOrigin().setZ(center.z() + 2.f);
                info.m_friction = .85f;
                info.m_linearDamping = .18f;
                info.m_angularDamping = .65f;
                part.mBody = std::make_unique<btRigidBody>(info);
                part.mBody->setUserPointer(this);
                part.mBody->setUserIndex(static_cast<int>(i));
                part.mBody->setSleepingThresholds(5.f, .25f);
                part.mBody->setCcdMotionThreshold(radius * .5f);
                part.mBody->setCcdSweptSphereRadius(radius * .55f);
                part.mBody->setLinearVelocity(Misc::Convert::toBullet(velocity));
                part.mRestReference = part.mBody->getWorldTransform();
                if (i == 0)
                    mInitialRootPosition = part.mRestReference.getOrigin();
                part.mParent = spec.mParent;

                if (i == 0)
                    continue;

                auto& parent = *mParts[spec.mParent].mBody;
                const auto& childTransform = part.mBody->getWorldTransform();
                const auto& parentTransform = parent.getWorldTransform();
                btTransform anchor = childTransform;
                // The capsules use Y and Bullet's twist limits use X.
                anchor.setRotation(rotation * btQuaternion(btVector3(0, 0, 1), SIMD_HALF_PI));
                anchor.setOrigin(Misc::Convert::toBullet(pivot) + btVector3(0.f, 0.f, 2.f));
                auto joint = std::make_unique<btConeTwistConstraint>(
                    parent, *part.mBody, parentTransform.inverse() * anchor, childTransform.inverse() * anchor);
                joint->setLimit(spec.mSwing, spec.mSideSwing, spec.mTwist);
                joint->setDamping(.2f);
                mJoints.push_back(std::move(joint));
                part.mInitialJointRotation = parentTransform.getRotation().inverse() * childTransform.getRotation();
            }

            mPhysics.withRagdollWorld([&](RagdollWorld& world) {
                for (const auto& part : mParts)
                    world.addRigidBody(part.mBody.get(), CollisionType_Ragdoll, CollisionType_RagdollScenery);
                for (const auto& joint : mJoints)
                    world.addConstraint(joint.get(), true);
                world.addRig(*this);
                mRegistered = true;
            });
            Log(Debug::Info) << "Ragdoll: " << mActorId << " activated " << mParts.size() << " bodies and "
                             << mJoints.size() << " joints (" << profile << ")";
            return true;
        }

        void updatePose()
        {
            if (!mControllersSuspended)
            {
                // Do this after ActorAnimation clears its external tracks.
                // Include unmodelled spine/neck bones between simulated parts.
                std::unordered_set<osg::Node*> visited;
                for (const Part& part : mParts)
                {
                    for (osg::Node* node : part.mParentPath)
                        if (visited.insert(node).second)
                            mControllers.suspend(*node);
                    if (visited.insert(part.mNode.get()).second)
                        mControllers.suspend(*part.mNode);
                }
                for (const auto& node : mAnimatedNodes)
                    if (visited.insert(node.get()).second)
                        mControllers.suspend(*node);
                mControllersSuspended = true;
            }

            for (std::size_t index : mPoseOrder)
            {
                const Part& part = mParts[index];
                const osg::Matrixf boneWorld = part.mBoneToBody * toMatrix(part.mBody->getWorldTransform());
                const osg::Matrixf parentWorld = osg::computeLocalToWorld(part.mParentPath);
                part.mNode->setMatrix(boneWorld * osg::Matrixf::inverse(parentWorld));
            }
        }

        bool advance(std::uint64_t, float elapsed) override
        {
            reportArticulation();
            return mPhysics.getRagdollWorld()->isGrabbed(*this) || updateRestState(elapsed);
        }

        void restorePose()
        {
            mControllers.restore();
            mControllersSuspended = false;
            for (const Part& part : mParts)
                part.mNode->setMatrix(part.mOriginalLocal);
        }

        void push(
            RagdollWorld& world, btRigidBody& body, const btVector3& point, const btVector3& direction, float strength)
        {
            const bool wasAsleep = !mAwake;
            if (!wake(world))
                return;

            btVector3 push = direction;
            push.setZ(.35f + std::max(btScalar(0), direction.z()) * .25f);
            const float speed = 70.f + std::clamp(strength, 0.f, 1.f) * 100.f;
            const btScalar impulse = speed * .55f * std::sqrt(mMass / body.getInvMass());
            body.applyImpulse(push * impulse, point - body.getWorldTransform().getOrigin());
            const Part& struck = mParts[static_cast<std::size_t>(body.getUserIndex())];
            Log(Debug::Info) << "Ragdoll: " << mActorId << " pushed at " << struck.mNode->getName()
                             << (wasAsleep ? " (woke)" : "");
        }

        void grab(
            RagdollWorld& world, const btVector3& point, const btVector3& eye, const btQuaternion& view, btScalar reach)
        {
            // The rendered hit can land on clothing or on a hand beyond a capsule.
            // Find the closest point on this corpse's collision shapes.
            Part* held = nullptr;
            btVector3 contact;
            btScalar bestDistance = BT_LARGE_FLOAT;
            for (Part& part : mParts)
            {
                const auto& transform = part.mBody->getWorldTransform();
                const btVector3 local = transform.invXform(point);
                const btVector3 axis(0, btClamped(local.y(), -part.mHalfLength, part.mHalfLength), 0);
                const btVector3 radial = local - axis;
                const btScalar length = radial.length();
                const btScalar distance = btMax(btScalar(0), length - part.mRadius);
                if (distance >= bestDistance)
                    continue;
                bestDistance = distance;
                held = &part;
                contact = distance > 0 ? transform * (axis + radial * (part.mRadius / length)) : point;
            }

            if (!held || (contact - eye).length2() > reach * reach || !wake(world))
                return;

            auto& body = *held->mBody;
            const btScalar impulse = btClamped(8 * btSqrt(mMass / body.getInvMass()), btScalar(8), btScalar(160));
            world.grab(*this, body, contact, eye, view, impulse);
            Log(Debug::Info) << "Ragdoll grab: " << mActorId << " held at " << held->mNode->getName();
        }

    private:
        bool wake(RagdollWorld& world)
        {
            if (!mAwake && !world.wakeRig(*this))
                return false;
            for (Part& part : mParts)
                part.mBody->activate(true);
            mAwake = true;
            mActivityTime = 0.f;
            mQuietTime = 0.f;
            return true;
        }

        struct Part
        {
            osg::ref_ptr<osg::MatrixTransform> mNode;
            osg::NodePath mParentPath;
            osg::Matrixf mOriginalLocal;
            osg::Matrixf mInitialWorld;
            osg::Matrixf mBoneToBody;
            osg::Vec3f mStart;
            osg::Vec3f mEnd;
            btTransform mRestReference;
            btQuaternion mInitialJointRotation;
            std::size_t mParent = 0;
            btScalar mRadius = 0;
            btScalar mHalfLength = 0;
            std::unique_ptr<btCollisionShape> mShape;
            std::unique_ptr<btRigidBody> mBody;
        };

        bool updateRestState(float elapsed)
        {
            const bool moved = std::ranges::any_of(mParts, [](const Part& part) {
                const auto& current = part.mBody->getWorldTransform();
                return (current.getOrigin() - part.mRestReference.getOrigin()).length2() > 1.f
                    || std::abs(current.getRotation().dot(part.mRestReference.getRotation())) < .99975f;
            });
            if (moved)
            {
                mQuietTime = 0.f;
                for (Part& part : mParts)
                    part.mRestReference = part.mBody->getWorldTransform();
            }
            else
                mQuietTime += elapsed;

            mActivityTime += elapsed;
            if (mQuietTime < .8f && mActivityTime < 16.f
                && std::ranges::any_of(mParts, [](const Part& part) { return part.mBody->isActive(); }))
                return true;

            for (const Part& part : mParts)
            {
                part.mBody->setLinearVelocity(btVector3(0.f, 0.f, 0.f));
                part.mBody->setAngularVelocity(btVector3(0.f, 0.f, 0.f));
                part.mBody->forceActivationState(ISLAND_SLEEPING);
            }
            mAwake = false;
            Log(Debug::Info) << "Ragdoll: " << mActorId << " settled";
            return false;
        }

        void reportArticulation()
        {
            if (mReportedArticulation)
                return;
            if (mParts.size() == 1
                && (mParts[0].mBody->getWorldTransform().getOrigin() - mInitialRootPosition).length2() > 4.f)
            {
                Log(Debug::Info) << "Ragdoll: " << mActorId << " moved whole model";
                mReportedArticulation = true;
                return;
            }
            for (std::size_t i = 1; i < mParts.size(); ++i)
            {
                const Part& child = mParts[i];
                const Part& parent = mParts[child.mParent];
                const btQuaternion relative = parent.mBody->getWorldTransform().getRotation().inverse()
                    * child.mBody->getWorldTransform().getRotation();
                const auto angle = (child.mInitialJointRotation.inverse() * relative).getAngleShortestPath();
                if (angle > .15f)
                {
                    Log(Debug::Info) << "Ragdoll: " << mActorId << " articulated at " << child.mNode->getName();
                    mReportedArticulation = true;
                    return;
                }
            }
        }

        PhysicsSystem& mPhysics;
        ESM::RefId mActorId;
        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> mRoot;
        std::unique_ptr<RagdollModelFrame> mModelFrame;
        std::vector<Part> mParts;
        std::vector<osg::ref_ptr<osg::Node>> mAnimatedNodes;
        std::vector<std::size_t> mPoseOrder;
        std::vector<std::unique_ptr<btConeTwistConstraint>> mJoints;
        RagdollControllers mControllers;
        bool mControllersSuspended = false;

        btVector3 mInitialRootPosition;
        float mMass = 0.f;
        float mActivityTime = 0.f;
        float mQuietTime = 0.f;
        bool mRegistered = false;
        bool mAwake = true;
        bool mReportedArticulation = false;
    };

    Ragdoll::Ragdoll(std::unique_ptr<Rig> rig)
        : mRig(std::move(rig))
    {
    }

    Ragdoll::~Ragdoll() = default;

    bool Ragdoll::enabled()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("OPENMW_RAGDOLLS");
            return value && std::string_view(value) == "1";
        }();
        return enabled;
    }

    std::unique_ptr<Ragdoll> Ragdoll::create(
        const MWWorld::Ptr& actor, const MWRender::Animation& animation, const osg::Vec3f& velocity)
    {
        auto* physics = MWBase::Environment::get().getWorld()->getRagdollPhysics();
        if (!physics || !physics->getRagdollWorld()->hasCapacity())
            return {};

        auto rig = std::make_unique<Rig>(*physics, actor);
        if (!rig->initialize(animation, velocity))
            return {};
        return std::unique_ptr<Ragdoll>(new Ragdoll(std::move(rig)));
    }

    void Ragdoll::updatePose()
    {
        mRig->updatePose();
    }

    void Ragdoll::restorePose() const
    {
        mRig->restorePose();
    }

    void Ragdoll::hit(const MWWorld::Ptr& player, float reach, float attackStrength)
    {
        const auto world = MWBase::Environment::get().getWorld();
        auto* physics = world->getRagdollPhysics();
        if (!physics || !physics->getRagdollWorld()->hasRigs())
            return;

        const auto& position = player.getRefData().getPosition();
        const osg::Vec3f halfExtents = world->getHalfExtents(player, true);
        const osg::Vec3f eye = position.asVec3() + osg::Vec3f(0.f, 0.f, 1.7f * halfExtents.z());
        const osg::Quat aim = osg::Quat(position.rot[0], osg::Vec3f(-1.f, 0.f, 0.f))
            * osg::Quat(position.rot[2], osg::Vec3f(0.f, 0.f, -1.f));
        const btVector3 direction = Misc::Convert::toBullet(aim * osg::Vec3f(0.f, 1.f, 0.f));
        const btVector3 from = Misc::Convert::toBullet(eye);
        const btVector3 to = from + direction * (reach + halfExtents.x() + .5f * halfExtents.z());
        physics->withRagdollWorld([&](RagdollWorld& dynamics) {
            const PartHit hit = findPart(dynamics, from, to, 4.f);
            if (hit.mBody)
                static_cast<Rig*>(hit.mBody->getUserPointer())
                    ->push(dynamics, *hit.mBody, hit.mPoint, direction, attackStrength);
        });
    }

    bool Ragdoll::canGrab(const MWWorld::Ptr& corpse)
    {
        return grabbableCorpse(corpse) != nullptr;
    }

    void Ragdoll::grab(const MWWorld::Ptr& player, const MWWorld::Ptr& corpse, const osg::Vec3f& point)
    {
        const auto world = MWBase::Environment::get().getWorld();
        auto* physics = world->getRagdollPhysics();
        Ragdoll* ragdoll = grabbableCorpse(corpse);
        if (!physics || !ragdoll)
            return;

        const auto& camera = *world->getCamera();
        const btVector3 eye = Misc::Convert::toBullet(grabEye(*world, player, camera));
        const btVector3 target = Misc::Convert::toBullet(point);
        const btScalar reach = world->getMaxActivationDistance();
        if ((target - eye).length2() > reach * reach)
            return;

        const btQuaternion view = Misc::Convert::toBullet(camera.getOrient());
        physics->withRagdollWorld([&](RagdollWorld& dynamics) {
            constexpr btScalar focusTolerance = 12;
            if ((target - dynamics.clipGrabTarget(eye, target)).length2() <= focusTolerance * focusTolerance)
                ragdoll->mRig->grab(dynamics, target, eye, view, reach);
        });
    }

    void Ragdoll::moveGrab(const MWWorld::Ptr& player, float dt)
    {
        const auto world = MWBase::Environment::get().getWorld();
        auto* physics = world->getRagdollPhysics();
        if (!physics || !physics->getRagdollWorld()->hasGrab())
            return;

        const auto& camera = *world->getCamera();
        if (camera.getMode() != MWRender::Camera::Mode::FirstPerson
            && camera.getMode() != MWRender::Camera::Mode::ThirdPerson)
        {
            releaseGrab();
            return;
        }

        const auto eye = Misc::Convert::toBullet(grabEye(*world, player, camera));
        const auto view = Misc::Convert::toBullet(camera.getOrient());
        physics->withRagdollWorld(
            [&](RagdollWorld& dynamics) { dynamics.moveGrab(eye, view, world->getMaxActivationDistance(), dt); });
    }

    void Ragdoll::releaseGrab()
    {
        auto* physics = MWBase::Environment::get().getWorld()->getRagdollPhysics();
        if (physics && physics->getRagdollWorld()->hasGrab())
            physics->withRagdollWorld([](RagdollWorld& world) { world.releaseGrab(); });
    }
}
