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
        const osg::Vec3f turn = spin.advance(1.f / 120.f, Rules::Support::Air);
        EXPECT_GT(turn.y(), 0.f);
        EXPECT_LT(turn.length(), .01f);
    }

    TEST(ItemPhysicsRules, LandingTumbleIsVisibleButFadesOnTheGround)
    {
        Rules::Spin spin;
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(1e6f, 0, 0), 2.f, 2.f);
        EXPECT_FLOAT_EQ(spin.velocity().length(), Rules::MaxSpin);
        float turned = 0.f;
        for (int step = 0; step < 240; ++step)
            turned += spin.advance(1.f / 120.f, Rules::Support::Ground).length();
        // The hardest landing turns the item over once or twice, then stops.
        EXPECT_GT(turned, 3.f);
        EXPECT_LT(turned, 10.f);
        EXPECT_LT(spin.velocity().length(), Rules::MaxSpin * .01f);
    }

    TEST(ItemPhysicsRules, HeldItemsStopTumblingAtOnce)
    {
        Rules::Spin spin;
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(1e6f, 0, 0), 2.f, 2.f);
        float turned = 0.f;
        for (int step = 0; step < 120; ++step)
            turned += spin.advance(1.f / 120.f, Rules::Support::Held).length();
        EXPECT_LT(turned, 1.5f);
        EXPECT_LT(spin.velocity().length(), 1e-6f);
    }

    TEST(ItemPhysicsRules, AirborneTumbleDecaysAndNeverGrowsWithoutAnImpact)
    {
        Rules::Spin spin;
        EXPECT_EQ(spin.advance(1.f / 60.f, Rules::Support::Air), osg::Vec3f());
        spin.impact(osg::Vec3f(0, 0, 1), osg::Vec3f(0, 200, 0), 2.f, 12.f);
        const float initial = spin.velocity().length();
        for (int step = 0; step < 600; ++step)
            spin.advance(1.f / 60.f, Rules::Support::Air);
        EXPECT_LT(spin.velocity().length(), initial * .4f);
        EXPECT_GT(spin.velocity().length(), initial * .3f);
    }

    // Rotates v by turn (axis * radians), as the item body does.
    osg::Vec3f rotated(const osg::Vec3f& v, const osg::Vec3f& turn)
    {
        const float angle = turn.length();
        if (angle == 0.f)
            return v;
        osg::Vec3f axis = turn;
        axis.normalize();
        return v * std::cos(angle) + (axis ^ v) * std::sin(angle) + axis * ((axis * v) * (1.f - std::cos(angle)));
    }

    float angleBetween(osg::Vec3f a, osg::Vec3f b)
    {
        a.normalize();
        b.normalize();
        return std::acos(std::clamp(a * b, -1.f, 1.f));
    }

    float settleFor(osg::Vec3f& up, const osg::Vec3f& normal, float seconds)
    {
        float turned = 0.f;
        for (int step = 0; step < static_cast<int>(seconds * 60.f); ++step)
        {
            const osg::Vec3f turn = Rules::settleTurn(up, normal, 1.f / 60.f);
            turned += turn.length();
            up = rotated(up, turn);
        }
        return turned;
    }

    TEST(ItemPhysicsRules, TiltedItemOnTheGroundComesToLieFlat)
    {
        osg::Vec3f up(0.f, std::sin(.8f), std::cos(.8f));
        const float turned = settleFor(up, osg::Vec3f(0, 0, 1), 1.f);
        EXPECT_NEAR(turned, .8f, .01f);
        EXPECT_LT(angleBetween(up, osg::Vec3f(0, 0, 1)), .01f);
    }

    TEST(ItemPhysicsRules, ItemSettlesAlongTheSlopeItRestsOn)
    {
        const osg::Vec3f slope(std::sin(.3f), 0.f, std::cos(.3f));
        osg::Vec3f up(0, 0, 1);
        settleFor(up, slope, 1.f);
        EXPECT_LT(angleBetween(up, slope), .01f);
    }

    TEST(ItemPhysicsRules, ItemLyingUpsideDownStaysUpsideDown)
    {
        EXPECT_EQ(Rules::settleTurn(osg::Vec3f(0, 0, -1), osg::Vec3f(0, 0, 1), 1.f / 60.f), osg::Vec3f());
        osg::Vec3f up(0.f, std::sin(.3f), -std::cos(.3f));
        settleFor(up, osg::Vec3f(0, 0, 1), 1.f);
        EXPECT_LT(angleBetween(up, osg::Vec3f(0, 0, -1)), .01f);
    }

    TEST(ItemPhysicsRules, CrimeAmountsMatchMinorAndDestructionPenalties)
    {
        EXPECT_EQ(Rules::crimeAmount(1), 25);
        EXPECT_EQ(Rules::crimeAmount(1000), 100);
        EXPECT_EQ(Rules::crimeAmount(1000, true), 2000);
    }
}
