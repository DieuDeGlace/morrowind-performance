#include "apps/openmw/mwrender/shatterrules.hpp"

#include <set>

#include <gtest/gtest.h>

namespace
{
    namespace Rules = MWRender::ShatterRules;

    TEST(ShatterRules, ModelsBreakIntoAFewShards)
    {
        EXPECT_EQ(Rules::shardCount(1), Rules::MinShards);
        EXPECT_EQ(Rules::shardCount(30), 5u);
        EXPECT_EQ(Rules::shardCount(5000), Rules::MaxShards);
    }

    TEST(ShatterRules, SeedsAreSpreadOverTheModel)
    {
        // Points along a bottle from base to neck.
        std::vector<osg::Vec3f> points;
        for (int i = 0; i <= 20; ++i)
            points.emplace_back(0.f, 0.f, static_cast<float>(i));
        const auto seeds = Rules::spreadSeeds(points, 3);
        ASSERT_EQ(seeds.size(), 3u);
        std::set<float> heights;
        for (auto seed : seeds)
            heights.insert(points[seed].z());
        // Both ends, then the middle.
        EXPECT_EQ(heights, (std::set<float>{ 0.f, 10.f, 20.f }));
        EXPECT_EQ(Rules::nearestSeed(osg::Vec3f(0, 0, 2), points, seeds),
            static_cast<std::size_t>(std::distance(seeds.begin(),
                std::find_if(seeds.begin(), seeds.end(), [&](auto s) { return points[s].z() == 0.f; }))));
    }

    TEST(ShatterRules, DuplicatePointsDoNotMakeEmptyShards)
    {
        const std::vector<osg::Vec3f> points(5, osg::Vec3f(1, 2, 3));
        EXPECT_EQ(Rules::spreadSeeds(points, 4).size(), 1u);
        EXPECT_TRUE(Rules::spreadSeeds({}, 4).empty());
    }

    TEST(ShatterRules, ShardsBounceThenRestOnTheFloor)
    {
        Rules::Motion motion{ osg::Vec3f(0, 0, 50), osg::Vec3f(90, 0, 70), 8.f };
        bool bounced = false;
        for (int step = 0; step < 300; ++step)
        {
            const float before = motion.mVelocity.z();
            Rules::step(motion, 10.f, 1.f / 60.f);
            if (before < 0.f && motion.mVelocity.z() > 0.f)
                bounced = true;
            EXPECT_GE(motion.mPosition.z(), 10.f);
        }
        EXPECT_TRUE(bounced);
        EXPECT_TRUE(motion.mResting);
        EXPECT_FLOAT_EQ(motion.mPosition.z(), 10.f);
        EXPECT_EQ(motion.mVelocity, osg::Vec3f());
        EXPECT_GT(motion.mPosition.x(), 30.f);
    }

    TEST(ShatterRules, ShardsShrinkAwayAtTheEnd)
    {
        EXPECT_FLOAT_EQ(Rules::scaleAt(0.f), 1.f);
        EXPECT_FLOAT_EQ(Rules::scaleAt(Rules::Lifetime - Rules::FadeTime), 1.f);
        EXPECT_GT(Rules::scaleAt(Rules::Lifetime - .1f), 0.f);
        EXPECT_LT(Rules::scaleAt(Rules::Lifetime - .1f), 1.f);
        EXPECT_FLOAT_EQ(Rules::scaleAt(Rules::Lifetime), 0.f);
    }
}
