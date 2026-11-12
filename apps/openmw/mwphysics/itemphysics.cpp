#include "itemphysics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/NarrowPhaseCollision/btPersistentManifold.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <osg/ComputeBoundsVisitor>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/mathutil.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"

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
                const auto scaling = object->getShapeInstance()->mCollisionShape->getLocalScaling();
                const float requested = mPtr.getCellRef().getScale();
                for (unsigned int axis = 0; axis < 3; ++axis)
                    if (std::abs(scaling[axis]) > .000001)
                    {
                        const float ratio = requested / static_cast<float>(scaling[axis]);
                        mCenter[axis] *= ratio;
                        half[axis] *= std::abs(ratio);
                    }
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
            const osg::Vec3f box = ItemPhysicsRules::collisionHalfExtents(half);
            mLargestHalfExtent = std::max({ box.x(), box.y(), box.z() });
            const float smallest = std::min({ box.x(), box.y(), box.z() });
            mShape = std::make_unique<btBoxShape>(Misc::Convert::toBullet(box));
            mShape->setMargin(std::min(btScalar(.04), btScalar(smallest * .25f)));
            btVector3 inertia;
            mShape->calculateLocalInertia(mProperties.mMass, inertia);
            const osg::Quat rotation
                = node ? node->getAttitude() : Misc::Convert::makeOsgQuat(mPtr.getRefData().getPosition());
            const osg::Vec3f center = mInitialPosition + rotation * mCenter;
            mInitialTransform = btTransform(Misc::Convert::toBullet(rotation), Misc::Convert::toBullet(center));
            btRigidBody::btRigidBodyConstructionInfo info(mProperties.mMass, nullptr, mShape.get(), inertia);
            info.m_startWorldTransform = mInitialTransform;
            info.m_restitution = mProperties.mRestitution;
            info.m_friction = ItemPhysicsRules::Friction;
            info.m_rollingFriction = ItemPhysicsRules::RollingFriction;
            info.m_linearDamping = mProperties.mLinearDamping;
            info.m_angularDamping = mProperties.mAngularDamping;
            mBody = std::make_unique<btRigidBody>(info);
            mBody->setUserPointer(this);
            mBody->setSleepingThresholds(0, 0); // Use the Lua mod's one-second, linear-speed sleep rule.
            mBody->setFlags(mBody->getFlags() | BT_DISABLE_WORLD_GRAVITY);
            mBody->setGravity(btVector3(0, 0, -ItemPhysicsRules::Gravity));
            // Thin items such as books must not tunnel through the ground.
            mBody->setCcdMotionThreshold(smallest * .5f);
            mBody->setCcdSweptSphereRadius(smallest * .8f);
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
            mHadContact = false;
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
            mNeedsSync = true;
            mActivityTime += elapsed;
            bool contact = false;
            auto* dispatcher = mPhysics.getRagdollWorld()->getDispatcher();
            for (int index = 0; index < dispatcher->getNumManifolds(); ++index)
            {
                const auto* manifold = dispatcher->getManifoldByIndexInternal(index);
                if (manifold->getNumContacts() > 0
                    && (manifold->getBody0() == mBody.get() || manifold->getBody1() == mBody.get()))
                {
                    contact = true;
                    break;
                }
            }
            const bool grabbed = mPhysics.getRagdollWorld()->isGrabbed(*this);
            if (grabbed != mWasGrabbed)
            {
                mBody->setDamping(mProperties.mLinearDamping,
                    grabbed ? ItemPhysicsRules::HeldAngularDamping : mProperties.mAngularDamping);
                mWasGrabbed = grabbed;
            }
            mSnapshot = mBody->getWorldTransform();
            if (contact && !mHadContact && mPreviousSpeed >= 50.f && mSoundTime >= .1f)
            {
                mImpact = true;
                mSoundTime = 0.f;
                ++mOffenses;
                if (mPreviousSpeed >= ItemPhysicsRules::DamagingImpactSpeed)
                    ++mDamage;
            }
            mHadContact = contact;
            const bool held = grabbed;
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
            const float speed = ItemPhysicsRules::restSpeed(static_cast<float>(mBody->getLinearVelocity().length()),
                static_cast<float>(mBody->getAngularVelocity().length()), mLargestHalfExtent);
            if (!mSleep.update(speed, elapsed, held) && (held || mActivityTime < 30.f))
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
        std::unique_ptr<btBoxShape> mShape;
        std::unique_ptr<btRigidBody> mBody;
        btTransform mInitialTransform;
        btTransform mSnapshot;
        float mLargestHalfExtent = 0.f;
        bool mWasGrabbed = false;
        ItemPhysicsRules::SleepTimer mSleep;
        btCollisionObject* mMirror = nullptr;
        int mMirrorGroup = 0;
        int mMirrorMask = 0;
        std::uint64_t mUsed = 0;
        float mActivityTime = 0.f;
        float mCrimeTime = 0.f;
        float mPreviousSpeed = 0.f;
        float mSoundTime = 1.f;
        int mHitPoints = ItemPhysicsRules::HitPoints;
        int mDamage = 0;
        bool mImpact = false;
        bool mHadContact = false;
        std::vector<const btCollisionObject*> mIgnoredActors;
        bool mRegistered = false;
        bool mNeedsSync = false;
        bool mRemoved = false;
        bool mMovedLogged = false;
        bool mWasUnderwater = false;
        bool mCrimeReported = false;
        unsigned mOffenses = 0;
    };

    ItemPhysics::ItemPhysics(PhysicsSystem& physics, Resource::ResourceSystem* resources)
        : mPhysics(physics)
        , mResources(resources)
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
            for (const auto& script : MWBase::Environment::get().getESMStore()->getLuaScriptsCfg().mScripts)
            {
                const auto path = script.mScriptPath.value();
                const bool global
                    = Misc::StringUtils::ciEqual(path, "scripts/maxyari/luaphysics/physicsengineglobal.lua")
                    && (script.mFlags & ESM::LuaScriptCfg::sGlobal);
                const bool local = Misc::StringUtils::ciEqual(path, "scripts/maxyari/luaphysics/physicsenginelocal.lua")
                    && (!script.mTypes.empty() || !script.mRecords.empty() || !script.mRefs.empty());
                if (global || local)
                {
                    mEnabled = false;
                    Log(Debug::Info)
                        << "Item physics: Lua Physics scripts are configured; leaving item simulation to Lua";
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
        bool initialized = false;
        mPhysics.withRagdollWorld([&](RagdollWorld&) { initialized = body->initialize(); });
        if (!initialized)
            return {};
        body->mUsed = ++mSerial;
        body->mHitPoints = breakable(item) ? ItemPhysicsRules::GlassHitPoints : ItemPhysicsRules::HitPoints;
        mBodies.emplace(item.mRef, body);
        return body;
    }

    bool ItemPhysics::applyImpulse(const MWWorld::Ptr& item, const osg::Vec3f& impulse, const MWWorld::Ptr& culprit)
    {
        if (!finite(impulse) || impulse.length2() > 1e12f)
            return false;
        const auto body = getOrCreate(item);
        if (!body)
            return false;
        if (!body->mRegistered && std::count_if(mBodies.begin(), mBodies.end(), [](const auto& entry) {
                return entry.second->mRegistered;
            }) >= MaxAwake)
            return false;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) {
            body->wake(world);
            body->mCulprit = culprit;
            body->mBody->applyCentralImpulse(Misc::Convert::toBullet(impulse));
        });
        return true;
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
            // A surface point can be far outside the small item collision proxy.
            // Pull through the centre of mass to avoid injecting drag-induced spin.
            const auto& center = body->mBody->getCenterOfMassPosition();
            world.grab(*body, *body->mBody, center, from, Misc::Convert::toBullet(view), ItemPhysicsRules::DragImpulse);
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

    bool ItemPhysics::hit(const MWWorld::Ptr& player, const MWWorld::Ptr& target, const osg::Vec3f& point,
        const osg::Vec3f& eye, const osg::Vec3f& direction, float reach, float strength)
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
            const auto contact = Misc::Convert::toBullet(point);
            if ((contact - from).length() > reach + 4.f)
                return;
            if ((contact - world.clipGrabTarget(from, contact)).length() > body->mProperties.mRadius + 4.f)
                return;
            body->wake(world);
            body->mCulprit = player;
            const float impulse = strength >= .9f ? 600.f : 300.f;
            body->mBody->applyCentralImpulse(Misc::Convert::toBullet(direction + osg::Vec3f(0, 0, 1)) * impulse);
            ++body->mOffenses;
            body->mDamage += strength >= .9f ? 10 : 5;
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
        actors.reserve(mPhysics.getActors().size());
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
                // Remove stale collider addresses before actors can be unloaded
                // or an allocator can reuse an address for world scenery.
                for (const auto* collider : body->mIgnoredActors)
                    body->mBody->setIgnoreCollisionCheck(collider, false);
                body->mIgnoredActors.clear();
                for (const auto& actor : actors)
                    if (actor.mDead || (world.isGrabbed(*body) && actor.mPtr == player))
                    {
                        body->mBody->setIgnoreCollisionCheck(actor.mCollider, true);
                        body->mIgnoredActors.push_back(actor.mCollider);
                    }
                body->mCrimeTime += dt;
                body->mSoundTime += dt;
                body->mPreviousSpeed = static_cast<float>(body->mBody->getLinearVelocity().length());
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
                if (body->mRemoved)
                    continue;
                if (body->mImpact)
                {
                    body->mImpact = false;
                    MWBase::Environment::get().getSoundManager()->playSound3D(body->mPtr,
                        body->mPtr.getClass().getDownSoundId(body->mPtr),
                        std::clamp(body->mPreviousSpeed / 400.f, .15f, .8f), 1.f);
                }
                if (body->mDamage > 0)
                {
                    body->mHitPoints -= std::exchange(body->mDamage, 0);
                    if (body->mHitPoints <= 0 && fracture(body))
                        continue;
                }
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

    void ItemPhysics::buildDebrisIndex()
    {
        if (mDebrisIndexed)
            return;
        mDebrisIndexed = true;
        for (const auto& path : mResources->getVFS()->getRecursiveDirectoryIterator("meshes/debris/"))
        {
            if (path.extension() != "nif")
                continue;
            for (const auto& target : ItemPhysicsRules::debrisTargets(path.stem()))
                mDebris[target].push_back(path);
        }
    }

    void ItemPhysics::reportDestruction(const std::shared_ptr<Body>& body)
    {
        const auto& item = body->mPtr;
        const auto& culprit = body->mCulprit;
        MWWorld::Ptr victim;
        const auto mechanics = MWBase::Environment::get().getMechanicsManager();
        if (!culprit.isEmpty() && !mechanics->isAllowedToUse(culprit, item, victim))
            mechanics->commitCrime(culprit, victim, MWBase::MechanicsManager::OT_Pickpocket,
                item.getCellRef().getFaction(), ItemPhysicsRules::crimeAmount(item.getClass().getValue(item), true));
    }

    const std::vector<VFS::Path::Normalized>* ItemPhysics::findDebris(const MWWorld::Ptr& item)
    {
        buildDebrisIndex();
        const auto model = item.getClass().getCorrectedModel(item);
        std::string stem(model.stem());
        if (stem.starts_with("x"))
            stem.erase(0, 1);
        auto found = mDebris.find(stem);
        if (found == mDebris.end())
        {
            const auto separator = stem.find_last_of('_');
            if (separator != std::string::npos
                && std::all_of(stem.begin() + separator + 1, stem.end(), [](char c) { return c >= '0' && c <= '9'; }))
                found = mDebris.find(stem.substr(0, separator));
        }
        if (found == mDebris.end() || found->second.empty() || found->second.size() > 16)
            return nullptr;
        return &found->second;
    }

    bool ItemPhysics::breakable(const MWWorld::Ptr& item)
    {
        // Custom scripts can implement their own quest/destruction behaviour.
        const auto type = item.getType();
        if ((type != ESM::REC_MISC && type != ESM::REC_ALCH) || !item.getClass().getScript(item).empty())
            return false;
        if (type == ESM::REC_MISC && findDebris(item))
            return true;
        return ItemPhysicsRules::glassModel(item.getClass().getCorrectedModel(item).stem());
    }

    bool ItemPhysics::fracture(const std::shared_ptr<Body>& body)
    {
        const auto item = body->mPtr;
        if (!breakable(item))
            return false;
        const auto* debris = item.getType() == ESM::REC_MISC ? findDebris(item) : nullptr;
        if (debris)
        {
            const auto awake = std::count_if(
                mBodies.begin(), mBodies.end(), [](const auto& entry) { return entry.second->mRegistered; });
            if (static_cast<std::size_t>(awake) + debris->size() > MaxAwake + (body->mRegistered ? 1 : 0))
                debris = nullptr;
        }

        const auto world = MWBase::Environment::get().getWorld();
        const osg::Vec3f velocity(Misc::Convert::toOsg(body->mBody->getLinearVelocity()));
        if (!debris)
        {
            // Without installed debris meshes, glass breaks into shards cut
            // from its own model, which land on the floor below it.
            const osg::Vec3f centre(Misc::Convert::toOsg(body->mBody->getWorldTransform().getOrigin()));
            const auto below = mPhysics.castRay(
                centre, centre - osg::Vec3f(0, 0, 2000.f), { item }, {}, CollisionType_World | CollisionType_HeightMap);
            const float floor = below.mHit ? below.mHitPos.z() : centre.z() - 2000.f;
            if (!world->getRenderingManager()->shatterObject(item, velocity, floor))
                return false;
        }
        reportDestruction(body);
        MWBase::Environment::get().getSoundManager()->playSound3D(
            item.getRefData().getPosition().asVec3(), item.getClass().getDownSoundId(item), 1.f, .8f);
        if (!debris)
        {
            Log(Debug::Info) << "Item physics: " << item.getCellRef().getRefId() << " shattered";
            world->deleteObject(item);
            return true;
        }

        const auto store = MWBase::Environment::get().getESMStore();
        const ESM::Position position = item.getRefData().getPosition();
        const float scale = item.getCellRef().getScale();
        const auto culprit = body->mCulprit;

        // Copy paths: creating/removing bodies can empty the transient caches.
        const auto paths = *debris;
        const int count = item.getCellRef().getCount();
        for (std::size_t index = 0; index < paths.size(); ++index)
        {
            const auto& path = paths[index];
            ESM::RefId recordId;
            if (const auto cached = mDebrisRecords.find(std::string(path.value())); cached != mDebrisRecords.end())
                recordId = cached->second;
            else
            {
                ESM::Miscellaneous record;
                record.blank();
                record.mName = "Debris";
                record.mModel = Misc::ResourceHelpers::meshPathForESM3(path.value());
                record.mIcon = "m/debris.dds";
                recordId = store->insert(record)->mId;
                mDebrisRecords.emplace(std::string(path.value()), recordId);
            }
            MWWorld::ManualRef reference(*store, recordId, count);
            const auto fragment = world->placeObject(reference.getPtr(), item.getCell(), position);
            world->scaleObject(fragment, scale);
            const float angle = static_cast<float>(index) * 6.2831853f / static_cast<float>(paths.size());
            applyImpulse(
                fragment, velocity * 1.2f + osg::Vec3f(std::cos(angle) * 60.f, std::sin(angle) * 60.f, 50.f), culprit);
            mDebrisPieces.push_back(fragment);
        }
        Log(Debug::Info) << "Item physics: " << item.getCellRef().getRefId() << " fractured into " << paths.size()
                         << " pieces";
        world->deleteObject(item);
        // Bound debris clutter without touching pieces already picked up.
        while (mDebrisPieces.size() > 128)
        {
            const auto old = mDebrisPieces.front();
            mDebrisPieces.pop_front();
            if (old.isInCell() && old.getCellRef().getCount() > 0)
                world->deleteObject(old);
        }
        return true;
    }

    void ItemPhysics::remove(const MWWorld::Ptr& item)
    {
        const auto found = mBodies.find(item.mRef);
        if (found == mBodies.end())
            return;
        found->second->mRemoved = true;
        mPhysics.withRagdollWorld([&](RagdollWorld& world) { found->second->stop(world); });
        mBodies.erase(found);
        if (mBodies.empty())
        {
            mDebrisRecords.clear();
            mDebrisPieces.clear();
        }
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
