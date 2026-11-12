#include "apps/openmw/mwphysics/itemphysicsrules.hpp"

#include <memory>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <gtest/gtest.h>

namespace
{
    namespace Rules = MWPhysics::ItemPhysicsRules;

    TEST(ItemPhysicsRules, OnlyTheLuaModsDefaultInventoryTypesAreEligible)
    {
        for (int type : { ESM::REC_CLOT, ESM::REC_ARMO, ESM::REC_BOOK, ESM::REC_INGR, ESM::REC_MISC, ESM::REC_ALCH,
                 ESM::REC_WEAP, ESM::REC_APPA, ESM::REC_LOCK, ESM::REC_PROB, ESM::REC_REPA })
            EXPECT_TRUE(Rules::eligibleType(type));
        for (int type :
            { ESM::REC_STAT, ESM::REC_DOOR, ESM::REC_CONT, ESM::REC_ACTI, ESM::REC_NPC_, ESM::REC_CREA, ESM::REC_LIGH })
            EXPECT_FALSE(Rules::eligibleType(type));
    }

    TEST(ItemPhysicsRules, TinyItemsHaveFiniteCollisionSizeAndMass)
    {
        const auto props = Rules::properties(osg::Vec3f(.001f, .01f, .1f));
        EXPECT_FLOAT_EQ(props.mRadius, 2.f);
        EXPECT_FLOAT_EQ(props.mMass, 1.f);
        EXPECT_FLOAT_EQ(props.mRestitution, .5f);
        EXPECT_EQ(Rules::collisionHalfExtents(osg::Vec3f(.001f, 5.f, .1f)), osg::Vec3f(1.f, 5.f, 1.f));
    }

    TEST(ItemPhysicsRules, SizeAndMaterialDetermineMassInsteadOfItemValue)
    {
        const osg::Vec3f half(70, 70, 70);
        const auto ordinary = Rules::properties(half);
        const auto metal = Rules::properties(half, true);
        EXPECT_NEAR(ordinary.mMass, 25.f, .02f);
        EXPECT_FLOAT_EQ(metal.mMass, ordinary.mMass * 2.f);
    }

    TEST(ItemPhysicsRules, InstalledDebrisSelectorsKeepTheirExplicitModelNumbers)
    {
        EXPECT_EQ(Rules::debrisTargets("misc_com_bottle_&05&06__blue_chunk_0"),
            (std::vector<std::string>{ "misc_com_bottle_05", "misc_com_bottle_06" }));
        EXPECT_EQ(
            Rules::debrisTargets("misc_com_bottle__brown_chunk_5"), (std::vector<std::string>{ "misc_com_bottle" }));
        EXPECT_EQ(Rules::debrisTargets("xmisc_com_jug_01__chunk_0"), (std::vector<std::string>{ "misc_com_jug_01" }));
    }

    TEST(ItemPhysicsRules, GlassVesselsShatterButCeramicAndMetalDoNot)
    {
        for (const char* model : { "Misc_Com_Bottle_07", "misc_potion_bargain_01", "misc_de_glass_green_01",
                 "misc_flask_02", "misc_skooma_vial" })
            EXPECT_TRUE(Rules::glassModel(model)) << model;
        for (const char* model : { "misc_lw_flask", "misc_com_metal_goblet_01", "misc_com_redware_cup", "text_folio_03",
                 "misc_de_pot_redware_01" })
            EXPECT_FALSE(Rules::glassModel(model)) << model;
    }

    TEST(ItemPhysicsRules, HeldOrMovingItemsCannotSleep)
    {
        Rules::SleepTimer timer;
        for (int frame = 0; frame < 120; ++frame)
            EXPECT_FALSE(timer.update(0.f, 1.f / 60.f, true));
        for (int frame = 0; frame < 120; ++frame)
            EXPECT_FALSE(timer.update(8.f, 1.f / 60.f, false));
        bool asleep = false;
        for (int frame = 0; frame < 61; ++frame)
            asleep = timer.update(0.f, 1.f / 60.f, false);
        EXPECT_TRUE(asleep);
        EXPECT_FALSE(timer.update(10.f, 1.f / 60.f, false));
    }

    TEST(ItemPhysicsRules, TumblingItemsAreNotAtRest)
    {
        EXPECT_FLOAT_EQ(Rules::restSpeed(1.f, 2.f, 10.f), 20.f);
        EXPECT_FLOAT_EQ(Rules::restSpeed(30.f, .1f, 10.f), 30.f);
    }

    TEST(ItemPhysicsRules, BreakingPublicPropertyRepeatedlyIsACrime)
    {
        Rules::PublicDamage damage;
        EXPECT_FALSE(damage.broke());
        EXPECT_FALSE(damage.broke());
        EXPECT_TRUE(damage.broke());
        damage.reported();
        EXPECT_FLOAT_EQ(damage.rating(), 0.f);

        // Two breaks, then a long wait: the next break is forgiven.
        damage.broke();
        damage.broke();
        for (int second = 0; second < 15; ++second)
            damage.update(1.f);
        EXPECT_FALSE(damage.broke());
    }

    TEST(ItemPhysicsRules, CrimeAmountsMatchMinorAndDestructionPenalties)
    {
        EXPECT_EQ(Rules::crimeAmount(1), 25);
        EXPECT_EQ(Rules::crimeAmount(1000), 100);
        EXPECT_EQ(Rules::crimeAmount(1000, true), 2000);
    }

    // Drops an item body, configured as the game does, onto flat ground at
    // z = 0 and lets it come to rest.
    class ItemOnGround : public testing::Test
    {
    protected:
        ItemOnGround()
            : mDispatcher(&mConfiguration)
            , mWorld(&mDispatcher, &mBroadphase, &mSolver, &mConfiguration)
            , mGroundShape(btVector3(0, 0, 1), 0)
            , mGround(0.f, nullptr, &mGroundShape)
        {
            mWorld.setGravity(btVector3(0, 0, -Rules::Gravity));
            mGround.setFriction(1.f);
            mWorld.addRigidBody(&mGround);
        }

        ~ItemOnGround() override
        {
            if (mBody)
                mWorld.removeRigidBody(mBody.get());
            mWorld.removeRigidBody(&mGround);
        }

        btRigidBody& drop(const osg::Vec3f& half, const btQuaternion& rotation, float height)
        {
            const auto props = Rules::properties(half);
            const osg::Vec3f box = Rules::collisionHalfExtents(half);
            mShape = std::make_unique<btBoxShape>(btVector3(box.x(), box.y(), box.z()));
            btVector3 inertia;
            mShape->calculateLocalInertia(props.mMass, inertia);
            btRigidBody::btRigidBodyConstructionInfo info(props.mMass, nullptr, mShape.get(), inertia);
            info.m_startWorldTransform = btTransform(rotation, btVector3(0, 0, height));
            info.m_restitution = props.mRestitution;
            info.m_friction = Rules::Friction;
            info.m_rollingFriction = Rules::RollingFriction;
            info.m_linearDamping = props.mLinearDamping;
            info.m_angularDamping = props.mAngularDamping;
            mBody = std::make_unique<btRigidBody>(info);
            mBody->setSleepingThresholds(0, 0);
            mWorld.addRigidBody(mBody.get());
            return *mBody;
        }

        void run(float seconds)
        {
            for (int step = 0; step < static_cast<int>(seconds * 120.f); ++step)
                mWorld.stepSimulation(1.f / 120.f, 1, 1.f / 120.f);
        }

        // Angle between the item's model up axis and world up, either way up.
        float tilt() const
        {
            const btVector3 up = quatRotate(mBody->getWorldTransform().getRotation(), btVector3(0, 0, 1));
            return static_cast<float>(std::acos(std::min(btScalar(1), std::abs(up.z()))));
        }

        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher;
        btDbvtBroadphase mBroadphase;
        btSequentialImpulseConstraintSolver mSolver;
        btDiscreteDynamicsWorld mWorld;
        btStaticPlaneShape mGroundShape;
        btRigidBody mGround;
        std::unique_ptr<btBoxShape> mShape;
        std::unique_ptr<btRigidBody> mBody;
    };

    TEST_F(ItemOnGround, StandingBottleRestsOnItsBaseInsteadOfSinking)
    {
        const btRigidBody& body = drop(osg::Vec3f(4, 4, 12), btQuaternion::getIdentity(), 20.f);
        run(3.f);
        EXPECT_NEAR(body.getWorldTransform().getOrigin().z(), 12.f, .5f);
        EXPECT_LT(tilt(), .05f);
        EXPECT_LT(body.getLinearVelocity().length(), 1.f);
    }

    TEST_F(ItemOnGround, TiltedBookLandsAndLiesFlat)
    {
        const btRigidBody& body = drop(osg::Vec3f(10, 7, 1.5f), btQuaternion(btVector3(1, 0, 0), 1.f), 30.f);
        run(4.f);
        EXPECT_LT(tilt(), .05f);
        EXPECT_NEAR(body.getWorldTransform().getOrigin().z(), 1.5f, .3f);
        EXPECT_LT(Rules::restSpeed(static_cast<float>(body.getLinearVelocity().length()),
                      static_cast<float>(body.getAngularVelocity().length()), 10.f),
            Rules::SleepSpeed);
    }

    TEST_F(ItemOnGround, SlidingBookStopsWithoutSpinning)
    {
        btRigidBody& body = drop(osg::Vec3f(10, 7, 1.5f), btQuaternion::getIdentity(), 1.6f);
        run(.5f);
        body.setLinearVelocity(btVector3(300, 0, 0));
        run(3.f);
        EXPECT_LT(body.getLinearVelocity().length(), 1.f);
        EXPECT_LT(body.getAngularVelocity().length(), .1f);
        EXPECT_LT(tilt(), .05f);
    }
}
