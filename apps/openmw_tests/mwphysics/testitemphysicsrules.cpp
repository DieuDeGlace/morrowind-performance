#include "apps/openmw/mwphysics/itemphysicsrules.hpp"

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

    TEST(ItemPhysicsRules, ImpactsStartATumbleAboutTheRollingAxis)
    {
        Rules::Spin spin;
        // Landing on flat ground while travelling along +X rolls forward about +Y.
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(100, 0, 0), 2.f, 12.f);
        EXPECT_NEAR(spin.velocity().x(), 0.f, 1e-4f);
        EXPECT_NEAR(spin.velocity().y(), 100.f, 1e-3f);
        EXPECT_NEAR(spin.velocity().z(), 0.f, 1e-4f);
        const osg::Vec3f turn = spin.advance(1.f / 120.f, false);
        EXPECT_GT(turn.y(), 0.f);
        EXPECT_LT(turn.length(), .01f);
    }

    TEST(ItemPhysicsRules, ItemsOnTheGroundOrInHandStopTumbling)
    {
        Rules::Spin spin;
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(1e6f, 0, 0), 2.f, 2.f);
        EXPECT_FLOAT_EQ(spin.velocity().length(), Rules::MaxSpin);
        float turned = 0.f;
        for (int step = 0; step < 120; ++step)
            turned += spin.advance(1.f / 120.f, true).length();
        // Even from the fastest possible tumble the item turns less than a
        // quarter of a revolution before lying still.
        EXPECT_LT(turned, 1.5f);
        EXPECT_LT(spin.velocity().length(), 1e-6f);
    }

    TEST(ItemPhysicsRules, AirborneTumbleDecaysAndNeverGrowsWithoutAnImpact)
    {
        Rules::Spin spin;
        EXPECT_EQ(spin.advance(1.f / 60.f, false), osg::Vec3f());
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(0, 200, 0), 2.f, 12.f);
        const float initial = spin.velocity().length();
        for (int step = 0; step < 600; ++step)
            spin.advance(1.f / 60.f, false);
        EXPECT_LT(spin.velocity().length(), initial * .4f);
        EXPECT_GT(spin.velocity().length(), initial * .3f);
    }

    TEST(ItemPhysicsRules, CrimeAmountsMatchMinorAndDestructionPenalties)
    {
        EXPECT_EQ(Rules::crimeAmount(1), 25);
        EXPECT_EQ(Rules::crimeAmount(1000), 100);
        EXPECT_EQ(Rules::crimeAmount(1000, true), 2000);
    }
}
