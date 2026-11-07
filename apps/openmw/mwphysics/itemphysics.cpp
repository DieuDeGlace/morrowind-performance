#include "itemphysics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <osg/ComputeBoundsVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/mathutil.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"

#include "actor.hpp"
#include "itemphysicsrules.hpp"
#include "object.hpp"
#include "physicssystem.hpp"
#include "ragdollworld.hpp"

namespace MWPhysics
{
    namespace
    {
#ifdef __ANDROID__
        constexpr std::size_t MaxAwake = 16;
        constexpr std::size_t MaxRetained = 64;
#else
        constexpr std::size_t MaxAwake = 32;
        constexpr std::size_t MaxRetained = 128;
#endif
        bool finite(const osg::Vec3f& vector)
        {
            return std::isfinite(vector.x()) && std::isfinite(vector.y()) && std::isfinite(vector.z());
        }
    }

    class ItemPhysics::Body final : public RagdollWorld::Island
    {
    public:
        Body(PhysicsSystem& physics, const MWWorld::Ptr& item)
            : mPhysics(physics)
            , mPtr(item)
            , mInitialPosition(item.getRefData().getPosition().asVec3())
        {
        }

        ~Body() override
        {
            if (mRegistered)
                mPhysics.withRagdollWorld([&](RagdollWorld& world) { stop(world); });
        }

        bool initialize()
        {
            auto* node = mPtr.getRefData().getBaseNode();
            osg::Vec3f half;
            if (const Object* object = mPhysics.getObject(mPtr))
            {
                btVector3 low, high;
                object->getShapeInstance()->mCollisionShape->getAabb(btTransform::getIdentity(), low, high);
                mCenter = osg::Vec3f(Misc::Convert::toOsg((high + low) * .5f));
                half = osg::Vec3f(Misc::Convert::toOsg((high - low) * .5f));
            }
            else
            {
                if (!node)
                    return false;
                // Skip the reference's position/rotation and fit in model space.
                osg::ComputeBoundsVisitor bounds;
                for (unsigned int child = 0; child < node->getNumChildren(); ++child)
                    node->getChild(child)->accept(bounds);
                const auto& box = bounds.getBoundingBox();
                if (!box.valid())
                    return false;
                const auto scale = node->getScale();
                mCenter = box.center();
                half = (box._max - box._min) * .5f;
                for (unsigned int axis = 0; axis < 3; ++axis)
                {
                    mCenter[axis] *= scale[axis];
                    half[axis] *= std::abs(scale[axis]);
                }
            }
            if (!finite(half) || !finite(mCenter) || half.length2() < .0001f || half.length2() > 1e10f)
                return false;
            mProperties = ItemPhysicsRules::properties(half);
            mShape = std::make_unique<btSphereShape>(mProperties.mRadius);
            btVector3 inertia;
            mShape->calculateLocalInertia(mProperties.mMass, inertia);
            const osg::Quat rotation
                = node ? node->getAttitude() : Misc::Convert::makeOsgQuat(mPtr.getRefData().getPosition());
            const osg::Vec3f center = mInitialPosition + rotation * mCenter;
            mInitialTransform = btTransform(Misc::Convert::toBullet(rotation), Misc::Convert::toBullet(center));
            btRigidBody::btRigidBodyConstructionInfo info(mProperties.mMass, nullptr, mShape.get(), inertia);
            info.m_startWorldTransform = mInitialTransform;
            info.m_restitution = mProperties.mRestitution;
            info.m_friction = .6f;
            info.m_linearDamping = mProperties.mLinearDamping;
            info.m_angularDamping = mProperties.mAngularDamping;
            mBody = std::make_unique<btRigidBody>(info);
            mBody->setUserPointer(this);
            mBody->setSleepingThresholds(0, 0); // Use the Lua mod's one-second, linear-speed sleep rule.
            mBody->setFlags(mBody->getFlags() | BT_DISABLE_WORLD_GRAVITY);
            mBody->setGravity(btVector3(0, 0, -ItemPhysicsRules::Gravity));
            mBody->setCcdMotionThreshold(mProperties.mRadius * .5f);
            mBody->setCcdSweptSphereRadius(mProperties.mRadius * .8f);
            mSnapshot = mInitialTransform;
            return true;
        }

        void changeMirrorGroup(RagdollWorld& world, bool moving)
        {
            const auto* object = mPhysics.getObject(mPtr);
            btCollisionObject* mirror = object ? object->getCollisionObject() : nullptr;
            if (moving)
            {
                if (!mirror || !mirror->getBroadphaseHandle())
                    return;
                mMirror = mirror;
                mMirrorGroup = mirror->getBroadphaseHandle()->m_collisionFilterGroup;
                mMirrorMask = mirror->getBroadphaseHandle()->m_collisionFilterMask;
                if (mMirrorGroup == CollisionType_VisualOnly)
                    return;
                // Keep the original picking shape and PtrHolder, but prevent
                // it colliding with its own dynamic body or blocking actors.
                world.removeCollisionObject(mirror);
                world.addCollisionObject(mirror, CollisionType_VisualOnly, mMirrorMask);
            }
            else
            {
                if (mirror && mirror == mMirror && mirror->getBroadphaseHandle()
                    && mMirrorGroup != CollisionType_VisualOnly)
                {
                    world.removeCollisionObject(mirror);
                    world.addCollisionObject(mirror, mMirrorGroup, mMirrorMask);
                }
                mMirror = nullptr;
            }
        }

        void wake(RagdollWorld& world)
        {
            if (!mRegistered)
            {
                changeMirrorGroup(world, true);
                world.addRigidBody(
                    mBody.get(), CollisionType_DynamicItem, CollisionType_RagdollScenery | CollisionType_Actor);
                world.addItem(*this);
                mRegistered = true;
                Log(Debug::Info) << "Item physics: " << mPtr.getCellRef().getRefId() << " activated";
            }
            mBody->activate(true);
            mSleep.reset();
            mActivityTime = 0.f;
        }

        void stop(RagdollWorld& world)
        {
            if (!mRegistered)
                return;
            world.removeItem(*this);
            world.removeRigidBody(mBody.get());
            changeMirrorGroup(world, false);
            mRegistered = false;
        }

        bool advance(std::uint64_t, float elapsed) override
        {
            mSnapshot = mBody->getWorldTransform();
            mNeedsSync = true;
            mActivityTime += elapsed;
            const bool held = mPhysics.getRagdollWorld()->isGrabbed(*this);
            const osg::Vec3f center(Misc::Convert::toOsg(mSnapshot.getOrigin()));
            if (!mMovedLogged
                && (center - osg::Vec3f(Misc::Convert::toOsg(mInitialTransform.getOrigin()))).length2() > 4.f)
            {
                Log(Debug::Info) << "Item physics: " << mPtr.getCellRef().getRefId() << " moved";
                mMovedLogged = true;
            }
            // Restore an item that escaped the world rather than losing it.
            if (!finite(center) || center.z() < mInitialPosition.z() - 7200.f)
            {
                mSnapshot = mInitialTransform;
                mBody->setWorldTransform(mSnapshot);
                mActivityTime = 31.f;
            }
            if (!mSleep.update(static_cast<float>(mBody->getLinearVelocity().length()), elapsed, held)
                && (held || mActivityTime < 30.f))
                return true;

            mBody->setLinearVelocity(btVector3(0, 0, 0));
            mBody->setAngularVelocity(btVector3(0, 0, 0));
            mBody->forceActivationState(ISLAND_SLEEPING);
            // advance() is erasing this island from the awake list itself.
            // Do not mutate that vector through removeItem() here.
            mPhysics.getRagdollWorld()->removeRigidBody(mBody.get());
            changeMirrorGroup(*mPhysics.getRagdollWorld(), false);
            mRegistered = false;
            Log(Debug::Info) << "Item physics: " << mPtr.getCellRef().getRefId() << " settled";
            return false;
        }

        PhysicsSystem& mPhysics;
        MWWorld::Ptr mPtr;
        MWWorld::Ptr mCulprit;
        osg::Vec3f mInitialPosition;
        osg::Vec3f mCenter;
        ItemPhysicsRules::Properties mProperties{};
        std::unique_ptr<btSphereShape> mShape;
        std::unique_ptr<btRigidBody> mBody;
        btTransform mInitialTransform;
        btTransform mSnapshot;
        ItemPhysicsRules::SleepTimer mSleep;
        btCollisionObject* mMirror = nullptr;
        int mMirrorGroup = 0;
        int mMirrorMask = 0;
        std::uint64_t mUsed = 0;
        float mActivityTime = 0.f;
        float mCrimeTime = 0.f;
        bool mRegistered = false;
        bool mNeedsSync = false;
        bool mRemoved = false;
        bool mMovedLogged = false;
        bool mWasUnderwater = false;
        bool mCrimeReported = false;
        unsigned mOffenses = 0;
    };

    ItemPhysics::ItemPhysics(PhysicsSystem& physics)
        : mPhysics(physics)
    {
    }

    ItemPhysics::~ItemPhysics() = default;

    bool ItemPhysics::enabled()
    {
        if (!mCompatibilityChecked)
        {
            mCompatibilityChecked = true;
            const char* value = std::getenv("OPENMW_ITEMS");
            mEnabled = !value || std::string_view(value) != "0";
            for (const std::string& content : MWBase::Environment::get().getWorld()->getContentFiles())
            {
                if (Misc::StringUtils::ciEqual(content, "LuaPhysicsEngine.omwscripts"))
                {
                    mEnabled = false;
                    Log(Debug::Info)
                        << "Item physics: LuaPhysicsEngine.omwscripts is active; leaving item simulation to Lua";
                    break;
                }
            }
        }
        return mEnabled;
    }

    bool ItemPhysics::canGrab(const MWWorld::Ptr& item)
    {
        return enabled() && !item.isEmpty() && ItemPhysicsRules::eligibleType(item.getType()) && item.isInCell()
            && item.getRefData().isEnabled() && item.getCellRef().getCount() > 0;
    }

    std::shared_ptr<ItemPhysics::Body> ItemPhysics::getOrCreate(const MWWorld::Ptr& item)
    {
        if (!canGrab(item))
            return {};
        if (const auto found = mBodies.find(item.mRef); found != mBodies.end())
        {
            found->second->mUsed = ++mSerial;
            return found->second;
        }
        if (mBodies.size() >= MaxRetained)
        {
            auto oldest = mBodies.end();
            for (auto it = mBodies.begin(); it != mBodies.end(); ++it)
                if (!it->second->mRegistered && (oldest == mBodies.end() || it->second->mUsed < oldest->second->mUsed))
                    oldest = it;
            if (oldest == mBodies.end())
                return {};
            mBodies.erase(oldest);
        }
        const auto& pos = item.getRefData().getPosition();
        MWBase::Environment::get().getWorld()->rotateObject(item, osg::Vec3f(pos.rot[0], pos.rot[1], pos.rot[2]));
        auto body = std::make_shared<Body>(mPhysics, item);
        if (!body->initialize())
            return {};
        body->mUsed = ++mSerial;
        mBodies.emplace(item.mRef, body);
        return body;
    }

    bool ItemPhysics::grab(const MWWorld::Ptr& player, const MWWorld::Ptr& item, const osg::Vec3f& point,
        const osg::Vec3f& eye, const osg::Quat& view, float reach)
    {
        const auto body = getOrCreate(item);
        if (!body || (point - eye).length2() > reach * reach)
            return false;
        if (!body->mRegistered && std::count_if(mBodies.begin(), mBodies.end(), [](const auto& entry) {
                return entry.second->mRegistered;
            }) >= MaxAwake)
            return false;
        bool grabbed = false;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) {
            const auto from = Misc::Convert::toBullet(eye);
            const auto target = Misc::Convert::toBullet(point);
            if ((target - world.clipGrabTarget(from, target)).length2() > 12.f * 12.f)
                return;
            body->wake(world);
            body->mCulprit = player;
            world.grab(*body, *body->mBody, target, from, Misc::Convert::toBullet(view), ItemPhysicsRules::DragImpulse);
            grabbed = true;
            Log(Debug::Info) << "Item physics: " << item.getCellRef().getRefId() << " grabbed";
        });
        if (grabbed)
            player.getClass().getCreatureStats(player).setDrawState(MWMechanics::DrawState::Nothing);
        return grabbed;
    }

    bool ItemPhysics::throwHeld(const MWWorld::Ptr& player, const osg::Quat& view)
    {
        bool thrown = false;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) {
            for (const auto& [_, body] : mBodies)
            {
                if (!world.isGrabbed(*body))
                    continue;
                world.releaseGrab();
                body->mCulprit = player;
                body->mBody->applyCentralImpulse(
                    Misc::Convert::toBullet(view * osg::Vec3f(0, 1, 0)) * ItemPhysicsRules::ThrowImpulse);
                body->mBody->activate(true);
                body->mSleep.reset();
                thrown = true;
                Log(Debug::Info) << "Item physics: " << body->mPtr.getCellRef().getRefId() << " thrown";
                break;
            }
        });
        return thrown;
    }

    bool ItemPhysics::hit(const MWWorld::Ptr& player, const MWWorld::Ptr& target, const osg::Vec3f& eye,
        const osg::Vec3f& direction, float reach, float strength)
    {
        const auto body = getOrCreate(target);
        if (!body)
            return false;
        if (!body->mRegistered && std::count_if(mBodies.begin(), mBodies.end(), [](const auto& entry) {
                return entry.second->mRegistered;
            }) >= MaxAwake)
            return false;
        bool hit = false;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) {
            const auto from = Misc::Convert::toBullet(eye);
            const auto center = body->mBody->getWorldTransform().getOrigin();
            if ((center - from).length() > reach + body->mProperties.mRadius)
                return;
            if ((center - world.clipGrabTarget(from, center)).length() > body->mProperties.mRadius + 4.f)
                return;
            body->wake(world);
            body->mCulprit = player;
            const float impulse = strength >= .9f ? 600.f : 300.f;
            body->mBody->applyCentralImpulse(Misc::Convert::toBullet(direction + osg::Vec3f(0, 0, 1)) * impulse);
            ++body->mOffenses;
            hit = true;
            Log(Debug::Info) << "Item physics: " << target.getCellRef().getRefId() << " pushed";
        });
        return hit;
    }

    void ItemPhysics::prepare(float dt)
    {
        if (mBodies.empty())
            return;
        const auto player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        struct ActorState
        {
            const btCollisionObject* mCollider;
            bool mDead;
            MWWorld::Ptr mPtr;
        };
        std::vector<ActorState> actors;
        for (const auto& [_, actor] : mPhysics.getActors())
            actors.push_back({ actor->getCollisionObject(),
                actor->getPtr().getClass().getCreatureStats(actor->getPtr()).isDead(), actor->getPtr() });

        mPhysics.withRagdollWorld([&](RagdollWorld& world) {
            for (const auto& [_, body] : mBodies)
            {
                if (!body->mRegistered)
                    continue;
                const float level = body->mPtr.getCell()->getWaterLevel();
                const bool underwater = body->mPtr.getCell()->getCell()->hasWater()
                    && body->mBody->getWorldTransform().getOrigin().z() < level;
                if (underwater && !body->mWasUnderwater)
                    body->mBody->setLinearVelocity(body->mBody->getLinearVelocity() * .5f);
                body->mWasUnderwater = underwater;
                body->mBody->setGravity(btVector3(
                    0, 0, -ItemPhysicsRules::Gravity * (underwater ? 1.f - body->mProperties.mBuoyancy : 1.f)));
                for (const auto& actor : actors)
                    body->mBody->setIgnoreCollisionCheck(
                        actor.mCollider, actor.mDead || (world.isGrabbed(*body) && actor.mPtr == player));
                body->mCrimeTime += dt;
            }
        });
    }

    void ItemPhysics::synchronize()
    {
        if (mBodies.empty())
            return;
        std::vector<std::shared_ptr<Body>> snapshot;
        for (const auto& [_, body] : mBodies)
            if (body->mNeedsSync)
                snapshot.push_back(body);
        const auto world = MWBase::Environment::get().getWorld();
        mSynchronizing = true;
        for (const auto& body : snapshot)
        {
            body->mNeedsSync = false;
            if (body->mRemoved || !body->mPtr.isInCell() || body->mPtr.getCellRef().getCount() <= 0)
                continue;
            const osg::Quat rotation = Misc::Convert::toOsg(body->mSnapshot.getRotation());
            const osg::Vec3f position
                = osg::Vec3f(Misc::Convert::toOsg(body->mSnapshot.getOrigin())) - rotation * body->mCenter;
            try
            {
                world->rotateObject(body->mPtr, Misc::toEulerAnglesZYX(rotation));
                body->mPtr = world->moveObject(body->mPtr, position);
                if (body->mRemoved || body->mCulprit.isEmpty() || body->mCrimeReported || body->mCrimeTime < .5f)
                    continue;
                body->mCrimeTime = 0.f;
                if (body->mCulprit.getCell() != body->mPtr.getCell())
                {
                    body->mCulprit = {};
                    continue;
                }
                if ((position - body->mInitialPosition).length() <= ItemPhysicsRules::OffensiveDistance
                    && body->mOffenses <= ItemPhysicsRules::MinorOffenseLimit)
                    continue;
                MWWorld::Ptr victim;
                const auto mechanics = MWBase::Environment::get().getMechanicsManager();
                if (mechanics->isAllowedToUse(body->mCulprit, body->mPtr, victim))
                    continue;
                body->mCrimeReported = mechanics->commitCrime(body->mCulprit, victim,
                    MWBase::MechanicsManager::OT_Pickpocket, body->mPtr.getCellRef().getFaction(),
                    ItemPhysicsRules::crimeAmount(body->mPtr.getClass().getValue(body->mPtr)));
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Item physics: could not update " << body->mPtr.getCellRef().getRefId() << ": "
                                    << e.what();
                remove(body->mPtr);
            }
        }
        mSynchronizing = false;
    }

    void ItemPhysics::remove(const MWWorld::Ptr& item)
    {
        const auto found = mBodies.find(item.mRef);
        if (found == mBodies.end())
            return;
        found->second->mRemoved = true;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) { found->second->stop(world); });
        mBodies.erase(found);
    }

    void ItemPhysics::externalTransform(const MWWorld::Ptr& item)
    {
        if (!mSynchronizing)
            remove(item);
    }

    void ItemPhysics::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        auto node = mBodies.extract(old.mRef);
        if (node.empty())
            return;
        node.mapped()->mPtr = updated;
        node.key() = updated.mRef;
        mBodies.insert(std::move(node));
    }
}
