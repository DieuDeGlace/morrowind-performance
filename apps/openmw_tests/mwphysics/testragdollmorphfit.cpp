#include "apps/openmw/mwphysics/ragdollmorphfit.hpp"
#include <gtest/gtest.h>

namespace
{
    MWPhysics::MorphFit::Pose cylinder()
    {
        MWPhysics::MorphFit::Pose p;
        for (int y = 0; y < 25; ++y)
            for (int i = 0; i < 8; ++i)
            {
                const float angle = i * 6.2831853f / 8;
                p.emplace_back(3.f * std::cos(angle), y * 3.f - 36.f, 3.f * std::sin(angle));
            }
        return p;
    }
    TEST(RagdollMorphFit, RecoversRigidRotationAndTranslationIncludingHalfTurn)
    {
        const auto p = cylinder();
        for (double angle : { .7, 3.141592653589793 })
        {
            const osg::Quat rotation(angle, osg::Vec3f(.3f, .8f, .4f));
            auto q = p;
            for (auto& v : q)
                v = rotation * v + osg::Vec3f(30, -14, 8);
            const auto fit = MWPhysics::MorphFit::rigid(p, q, std::vector<float>(p.size(), 1.f));
            for (std::size_t v = 0; v < p.size(); ++v)
                EXPECT_LT((fit.apply(p[v]) - q[v]).length(), .001f);
        }
    }
    TEST(RagdollMorphFit, DoesNotTurnRigidMorphMotionIntoExtraJoints)
    {
        const auto p = cylinder();
        auto q = p;
        for (auto& v : q)
            v += osg::Vec3f(12, -6, 50);
        EXPECT_TRUE(MWPhysics::MorphFit::fit(p, { p, q }).mPivots.empty());
    }
    TEST(RagdollMorphFit, FitsBentCompressedBodyWithNormalizedLocalInfluences)
    {
        const auto p = cylinder();
        std::vector<MWPhysics::MorphFit::Pose> poses;
        for (int f = 0; f < 9; ++f)
        {
            auto q = p;
            for (auto& v : q)
            {
                const float phase = f * .7f;
                v.z() += 8.f * std::sin(v.y() * .06f + phase);
                v.y() *= 1.f + .15f * std::sin(phase);
            }
            poses.push_back(std::move(q));
        }
        const auto fit = MWPhysics::MorphFit::fit(p, poses);
        ASSERT_GE(fit.mPivots.size(), 3);
        ASSERT_LE(fit.mPivots.size(), 6);
        ASSERT_EQ(fit.mWeights.size(), p.size());
        for (const auto& weights : fit.mWeights)
        {
            EXPECT_NEAR(std::accumulate(weights.begin(), weights.end(), 0.f), 1.f, .00001f);
            EXPECT_LE(std::count_if(weights.begin(), weights.end(), [](float w) { return w > 0; }), 3);
            for (float w : weights)
                EXPECT_TRUE(std::isfinite(w) && w >= 0 && w <= 1);
        }
        const auto rigid = MWPhysics::MorphFit::rigid(p, poses[3], std::vector<float>(p.size(), 1.f));
        const auto predicted
            = MWPhysics::MorphFit::predict(p, MWPhysics::MorphFit::solve(p, poses[3], fit.mWeights, 8), fit.mWeights);
        double a = 0, b = 0;
        for (std::size_t v = 0; v < p.size(); ++v)
        {
            a += (rigid.apply(p[v]) - poses[3][v]).length2();
            b += (predicted[v] - poses[3][v]).length2();
        }
        EXPECT_LT(b, a * .35);
    }
    TEST(RagdollMorphFit, RejectsMalformedAndNonFiniteGeometry)
    {
        MWPhysics::MorphFit::Pose oversized(MWPhysics::MorphFit::MaxVertices + 1);
        EXPECT_TRUE(MWPhysics::MorphFit::fit(oversized, { oversized, oversized }).mPivots.empty());
        const auto p = cylinder();
        auto bad = p;
        bad.back().x() = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(MWPhysics::MorphFit::fit(p, { p, bad }).mPivots.empty());
        bad.pop_back();
        EXPECT_TRUE(MWPhysics::MorphFit::fit(p, { p, bad }).mPivots.empty());
        EXPECT_TRUE(MWPhysics::MorphFit::fit(p, { p, p }, 0).mPivots.empty());
    }
}
