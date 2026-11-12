#ifndef OPENMW_MWRENDER_SHATTERRULES_HPP
#define OPENMW_MWRENDER_SHATTERRULES_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <osg/Vec3f>

namespace MWRender::ShatterRules
{
    constexpr std::size_t MinShards = 3;
    constexpr std::size_t MaxShards = 8;
    constexpr std::size_t TrianglesPerShard = 6;
    constexpr float Gravity = 9.8f * 69.99f;
    constexpr float Lifetime = 5.f;
    constexpr float FadeTime = .75f;
    constexpr float Bounce = .3f;
    // A shard hitting the floor slower than this stops bouncing.
    constexpr float RestSpeed = 25.f;

    inline std::size_t shardCount(std::size_t triangles)
    {
        return std::clamp(triangles / TrianglesPerShard, MinShards, MaxShards);
    }

    // Picks count well spread points (indices into points): the point
    // farthest from the mean first, then each time the point farthest from
    // all points picked so far. Deterministic, so a model always breaks the
    // same way.
    inline std::vector<std::size_t> spreadSeeds(const std::vector<osg::Vec3f>& points, std::size_t count)
    {
        std::vector<std::size_t> seeds;
        if (points.empty() || count == 0)
            return seeds;
        osg::Vec3f mean;
        for (const auto& point : points)
            mean += point;
        mean /= static_cast<float>(points.size());
        std::vector<float> distance(points.size());
        for (std::size_t i = 0; i < points.size(); ++i)
            distance[i] = (points[i] - mean).length2();
        while (seeds.size() < std::min(count, points.size()))
        {
            const auto next = static_cast<std::size_t>(
                std::distance(distance.begin(), std::max_element(distance.begin(), distance.end())));
            if (!seeds.empty() && distance[next] <= 0.f)
                break; // Only duplicates are left.
            seeds.push_back(next);
            // From now on, distance is to the nearest seed, not to the mean.
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const float toSeed = (points[i] - points[next]).length2();
                distance[i] = seeds.size() == 1 ? toSeed : std::min(distance[i], toSeed);
            }
            for (std::size_t seed : seeds)
                distance[seed] = -1.f;
        }
        return seeds;
    }

    // Index of the seed nearest to point.
    inline std::size_t nearestSeed(
        const osg::Vec3f& point, const std::vector<osg::Vec3f>& points, const std::vector<std::size_t>& seeds)
    {
        std::size_t best = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < seeds.size(); ++i)
        {
            const float distance = (points[seeds[i]] - point).length2();
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    struct Motion
    {
        osg::Vec3f mPosition;
        osg::Vec3f mVelocity;
        float mSpin = 0.f; // radians per second
        bool mResting = false;
    };

    // Advances a shard falling under gravity onto a floor at height floor.
    inline void step(Motion& motion, float floor, float dt)
    {
        dt = std::clamp(dt, 0.f, .05f);
        if (motion.mResting)
            return;
        motion.mVelocity.z() -= Gravity * dt;
        motion.mPosition += motion.mVelocity * dt;
        if (motion.mPosition.z() > floor)
            return;
        motion.mPosition.z() = floor;
        if (-motion.mVelocity.z() * Bounce < RestSpeed)
        {
            motion.mVelocity = osg::Vec3f();
            motion.mSpin = 0.f;
            motion.mResting = true;
            return;
        }
        motion.mVelocity.z() = -motion.mVelocity.z() * Bounce;
        motion.mVelocity.x() *= .5f;
        motion.mVelocity.y() *= .5f;
        motion.mSpin *= .5f;
    }

    // Shards shrink away over the last FadeTime seconds.
    inline float scaleAt(float age)
    {
        return std::clamp((Lifetime - age) / FadeTime, 0.f, 1.f);
    }
}

#endif
