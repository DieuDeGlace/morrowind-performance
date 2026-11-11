#ifndef OPENMW_MWPHYSICS_RAGDOLLMORPHFIT_H
#define OPENMW_MWPHYSICS_RAGDOLLMORPHFIT_H

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Quat>
#include <osg/Vec3f>

namespace MWPhysics::MorphFit
{
    constexpr std::size_t MaxVertices = 8192;
    constexpr std::size_t MaxTrainingVertices = 512;
    using Pose = std::vector<osg::Vec3f>;
    struct Rigid
    {
        osg::Quat mRotation;
        osg::Vec3f mTranslation;
        osg::Vec3f apply(const osg::Vec3f& p) const { return mRotation * p + mTranslation; }
    };
    using Weights = std::vector<std::vector<float>>;
    struct Layout
    {
        Pose mRest;
        Weights mWeights;
        Pose mPivots;
        std::vector<std::size_t> mParents;
        osg::Vec3f mLowTip;
        osg::Vec3f mHighTip;
        unsigned int mAxis = 0;
        float mError = 0.f;
    };
    inline bool finite(const osg::Vec3f& p)
    {
        return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z());
    }
    // Horn's symmetric quaternion problem. Jacobi rotations also handle a
    // half-turn (where power iteration started at identity can get stuck).
    inline Rigid rigid(const Pose& p, const Pose& q, const std::vector<float>& weights)
    {
        double total = 0;
        osg::Vec3d pc, qc;
        for (std::size_t v = 0; v < p.size(); ++v)
        {
            total += weights[v];
            pc += osg::Vec3d(p[v]) * weights[v];
            qc += osg::Vec3d(q[v]) * weights[v];
        }
        if (total < 1e-12)
            return {};
        pc /= total;
        qc /= total;
        double s[3][3]{};
        for (std::size_t v = 0; v < p.size(); ++v)
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    s[i][j] += weights[v] * (p[v][i] - pc[i]) * (q[v][j] - qc[j]);
        const double trace = s[0][0] + s[1][1] + s[2][2];
        double k[4][4] = { { trace, s[1][2] - s[2][1], s[2][0] - s[0][2], s[0][1] - s[1][0] },
            { s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2], s[0][1] + s[1][0], s[0][2] + s[2][0] },
            { s[2][0] - s[0][2], s[0][1] + s[1][0], -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1] },
            { s[0][1] - s[1][0], s[0][2] + s[2][0], s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2] } };
        double eigen[4][4]{};
        for (int i = 0; i < 4; ++i)
            eigen[i][i] = 1;
        for (int sweep = 0; sweep < 12; ++sweep)
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                {
                    if (std::abs(k[a][b]) < 1e-12)
                        continue;
                    const double angle = .5 * std::atan2(2 * k[a][b], k[b][b] - k[a][a]);
                    const double c = std::cos(angle), sn = std::sin(angle);
                    const double aa = k[a][a], bb = k[b][b], ab = k[a][b];
                    k[a][a] = c * c * aa - 2 * c * sn * ab + sn * sn * bb;
                    k[b][b] = sn * sn * aa + 2 * c * sn * ab + c * c * bb;
                    k[a][b] = k[b][a] = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (i != a && i != b)
                        {
                            const double ia = k[i][a], ib = k[i][b];
                            k[i][a] = k[a][i] = c * ia - sn * ib;
                            k[i][b] = k[b][i] = sn * ia + c * ib;
                        }
                        const double ia = eigen[i][a], ib = eigen[i][b];
                        eigen[i][a] = c * ia - sn * ib;
                        eigen[i][b] = sn * ia + c * ib;
                    }
                }
        int best = 0;
        for (int i = 1; i < 4; ++i)
            if (k[i][i] > k[best][best])
                best = i;
        Rigid result;
        result.mRotation.set(eigen[1][best], eigen[2][best], eigen[3][best], eigen[0][best]);
        result.mTranslation = osg::Vec3f(qc - result.mRotation * pc);
        return result;
    }
    inline Pose predict(const Pose& p, const std::vector<Rigid>& transforms, const Weights& w)
    {
        Pose result(p.size());
        for (std::size_t v = 0; v < p.size(); ++v)
            for (std::size_t b = 0; b < transforms.size(); ++b)
                result[v] += transforms[b].apply(p[v]) * w[v][b];
        return result;
    }
    inline std::vector<Rigid> solve(const Pose& p, const Pose& q, const Weights& w, int steps = 4)
    {
        const std::size_t count = w.front().size();
        std::vector<Rigid> transforms(count);
        std::vector<float> squared(p.size());
        for (std::size_t b = 0; b < count; ++b)
        {
            for (std::size_t v = 0; v < p.size(); ++v)
                squared[v] = w[v][b] * w[v][b];
            transforms[b] = rigid(p, q, squared);
        }
        Pose pred = predict(p, transforms, w), target(p.size());
        for (int step = 0; step < steps; ++step)
            for (std::size_t b = 0; b < count; ++b)
            {
                for (std::size_t v = 0; v < p.size(); ++v)
                {
                    squared[v] = w[v][b] * w[v][b];
                    target[v] = w[v][b] > .0001f ? (q[v] - pred[v]) / w[v][b] + transforms[b].apply(p[v]) : p[v];
                }
                const auto next = rigid(p, target, squared);
                for (std::size_t v = 0; v < p.size(); ++v)
                    pred[v] += (next.apply(p[v]) - transforms[b].apply(p[v])) * w[v][b];
                transforms[b] = next;
            }
        return transforms;
    }
    inline void simplex(std::vector<float>& value, const std::array<std::size_t, 3>& support)
    {
        const std::array<float, 3> original{ value[support[0]], value[support[1]], value[support[2]] };
        auto sorted = original;
        std::sort(sorted.begin(), sorted.end(), std::greater<float>());
        float sum = 0, theta = 0;
        for (std::size_t i = 0; i < 3; ++i)
        {
            sum += sorted[i];
            if (sorted[i] > (sum - 1.f) / static_cast<float>(i + 1))
                theta = (sum - 1.f) / static_cast<float>(i + 1);
        }
        std::fill(value.begin(), value.end(), 0.f);
        for (std::size_t i = 0; i < support.size(); ++i)
            value[support[i]] = std::max(0.f, original[i] - theta);
    }
    // Fit only when death physics is requested. Templates cache the result;
    // living actors never replace or approximate their original morph tracks.
    inline Layout fit(const Pose& fullRest, const std::vector<Pose>& fullPoses, std::size_t count = 6)
    {
        Layout result;
        if (fullRest.size() < 24 || fullRest.size() > MaxVertices || fullPoses.size() < 2 || count < 3 || count > 6)
            return result;
        for (const auto& q : fullPoses)
            if (q.size() != fullRest.size())
                return result;
        osg::BoundingBoxf box;
        for (const auto& p : fullRest)
        {
            if (!finite(p))
                return result;
            box.expandBy(p);
        }
        const auto extent = box._max - box._min;
        unsigned int axis = 0;
        for (unsigned int i = 1; i < 3; ++i)
            if (extent[i] > extent[axis])
                axis = i;
        const float length = extent[axis];
        if (!(length > .01f) || length > 100000.f)
            return result;
        Pose p;
        std::vector<Pose> poses(std::min<std::size_t>(64, fullPoses.size()));
        const std::size_t n = std::min(MaxTrainingVertices, fullRest.size());
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t v = i * (fullRest.size() - 1) / (n - 1);
            indices.push_back(v);
            p.push_back(fullRest[v]);
            for (std::size_t f = 0; f < poses.size(); ++f)
            {
                const auto point = fullPoses[f * (fullPoses.size() - 1) / (poses.size() - 1)][v];
                if (!finite(point))
                    return result;
                poses[f].push_back(point);
            }
        }
        // Rigid motion encoded as morph offsets is not anatomy to subdivide.
        std::vector<float> ones(n, 1.f);
        double nonRigid = 0;
        for (const auto& q : poses)
        {
            const auto r = rigid(p, q, ones);
            for (std::size_t v = 0; v < n; ++v)
                nonRigid += (r.apply(p[v]) - q[v]).length2();
        }
        if (std::sqrt(nonRigid / (n * poses.size())) < length * .003f)
            return result;
        count = std::min(count, n / 8);
        constexpr std::size_t bins = 18;
        std::array<float, bins + 1> cuts;
        for (std::size_t i = 0; i <= bins; ++i)
            cuts[i] = box._min[axis] + length * static_cast<float>(i) / bins;
        cuts.front() -= .001f;
        cuts.back() += .001f;
        double cost[bins + 1][bins + 1];
        for (auto& row : cost)
            std::fill(std::begin(row), std::end(row), std::numeric_limits<double>::infinity());
        for (std::size_t a = 0; a < bins; ++a)
            for (std::size_t b = a + 2; b <= bins; ++b)
            {
                std::vector<float> selected(n);
                std::size_t vertices = 0;
                for (std::size_t v = 0; v < n; ++v)
                    if (p[v][axis] >= cuts[a] && p[v][axis] <= cuts[b])
                    {
                        selected[v] = 1;
                        ++vertices;
                    }
                if (vertices < 8)
                    continue;
                double error = 0;
                for (const auto& q : poses)
                {
                    const auto r = rigid(p, q, selected);
                    for (std::size_t v = 0; v < n; ++v)
                        if (selected[v])
                            error += (r.apply(p[v]) - q[v]).length2();
                }
                cost[a][b] = error;
            }
        std::vector<std::array<double, bins + 1>> dp(count + 1);
        std::vector<std::array<std::size_t, bins + 1>> previous(count + 1);
        for (auto& row : dp)
            row.fill(std::numeric_limits<double>::infinity());
        dp[0][0] = 0;
        for (std::size_t k = 1; k <= count; ++k)
            for (std::size_t b = 1; b <= bins; ++b)
                for (std::size_t a = 0; a < b; ++a)
                    if (dp[k - 1][a] + cost[a][b] < dp[k][b])
                    {
                        dp[k][b] = dp[k - 1][a] + cost[a][b];
                        previous[k][b] = a;
                    }
        while (count > 3 && !std::isfinite(dp[count][bins]))
            --count;
        if (!std::isfinite(dp[count][bins]))
            return result;
        std::vector<float> bounds(count + 1);
        bounds[count] = cuts.back();
        for (std::size_t k = count, b = bins; k > 0; --k)
        {
            b = previous[k][b];
            bounds[k - 1] = cuts[b];
        }
        Pose centers(count);
        for (std::size_t b = 0; b < count; ++b)
        {
            float total = 0;
            for (const auto& point : p)
                if (point[axis] >= bounds[b] && point[axis] <= bounds[b + 1])
                {
                    centers[b] += point;
                    ++total;
                }
            centers[b] /= std::max(1.f, total);
        }
        Weights w(n, std::vector<float>(count)), prior = w;
        std::vector<std::array<std::size_t, 3>> support(n);
        for (std::size_t v = 0; v < n; ++v)
        {
            std::vector<std::size_t> nearest(count);
            std::iota(nearest.begin(), nearest.end(), 0);
            std::sort(nearest.begin(), nearest.end(), [&](auto a, auto b) {
                return std::abs(p[v][axis] - centers[a][axis]) < std::abs(p[v][axis] - centers[b][axis]);
            });
            float sum = 0;
            for (std::size_t j = 0; j < 3; ++j)
            {
                const auto b = nearest[j];
                support[v][j] = b;
                const float d
                    = (p[v][axis] - centers[b][axis]) / std::max(length * .07f, (bounds[b + 1] - bounds[b]) * .62f);
                sum += w[v][b] = std::exp(-.5f * d * d);
            }
            for (auto& value : w[v])
                value /= std::max(sum, 1e-12f);
        }
        prior = w;
        std::vector<std::vector<Rigid>> transforms(poses.size());
        for (int iteration = 0; iteration < 10; ++iteration)
        {
            for (std::size_t f = 0; f < poses.size(); ++f)
                transforms[f] = solve(p, poses[f], w, 3);
            for (std::size_t v = 0; v < n; ++v)
            {
                float h[6][6]{}, g[6]{};
                for (std::size_t f = 0; f < poses.size(); ++f)
                {
                    osg::Vec3f delta[6];
                    for (auto a : support[v])
                        delta[a] = transforms[f][a].apply(p[v]) - p[v];
                    const auto target = poses[f][v] - p[v];
                    for (auto a : support[v])
                    {
                        g[a] += delta[a] * target;
                        for (auto b : support[v])
                            h[a][b] += delta[a] * delta[b];
                    }
                }
                float upper = 0;
                const float scale = 1.f / (3.f * poses.size());
                for (auto a : support[v])
                {
                    g[a] *= scale;
                    float sum = .035f;
                    for (auto b : support[v])
                    {
                        h[a][b] *= scale;
                        sum += std::abs(h[a][b]);
                    }
                    upper = std::max(upper, sum);
                }
                auto next = w[v];
                for (int step = 0; step < 40; ++step)
                {
                    next = w[v];
                    for (auto a : support[v])
                    {
                        float grad = .035f * (w[v][a] - prior[v][a]) - g[a];
                        for (auto b : support[v])
                            grad += h[a][b] * w[v][b];
                        next[a] -= .95f * grad / std::max(upper, 1e-6f);
                    }
                    simplex(next, support[v]);
                    w[v].swap(next);
                }
            }
        }
        result.mRest = fullRest;
        result.mWeights.resize(fullRest.size(), std::vector<float>(count));
        for (std::size_t v = 0; v < fullRest.size(); ++v)
        {
            if (fullRest.size() == n)
                result.mWeights[v] = w[v];
            else
            {
                std::array<std::pair<float, std::size_t>, 3> near;
                near.fill({ std::numeric_limits<float>::infinity(), 0 });
                for (std::size_t i = 0; i < n; ++i)
                {
                    const float d = (fullRest[v] - p[i]).length2();
                    if (d < near[2].first)
                    {
                        near[2] = { d, i };
                        std::sort(near.begin(), near.end());
                    }
                }
                for (const auto& [distance, i] : near)
                    for (std::size_t b = 0; b < count; ++b)
                        result.mWeights[v][b] += w[i][b] / std::max(distance, 1e-6f);
            }
            auto& weights = result.mWeights[v];
            std::vector<std::size_t> order(count);
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](auto a, auto b) { return weights[a] > weights[b]; });
            for (std::size_t j = 3; j < count; ++j)
                weights[order[j]] = 0;
            float sum = std::accumulate(weights.begin(), weights.end(), 0.f);
            if (!(sum > 0) || !std::isfinite(sum))
                return {};
            for (auto& value : weights)
                value /= sum;
        }
        result.mAxis = axis;
        result.mPivots = centers;
        result.mParents.resize(count);
        const std::size_t root = (count - 1) / 2;
        for (std::size_t b = 0; b < count; ++b)
        {
            result.mParents[b] = b < root ? b + 1 : b > root ? b - 1 : root;
            if (b == root)
                continue;
            const float border = bounds[std::max(b, result.mParents[b])];
            osg::BoundingBoxf section;
            for (const auto& point : p)
                if (std::abs(point[axis] - border) < length * .06f)
                    section.expandBy(point);
            if (section.valid())
                result.mPivots[b] = section.center();
            result.mPivots[b][axis] = border;
        }
        auto tip = [&](bool high) {
            osg::BoundingBoxf section;
            const float end = high ? box._max[axis] : box._min[axis];
            for (const auto& point : p)
                if (std::abs(point[axis] - end) < length * .05f)
                    section.expandBy(point);
            auto point = section.valid() ? section.center() : box.center();
            point[axis] = end;
            return point;
        };
        result.mLowTip = tip(false);
        result.mHighTip = tip(true);
        double error = 0;
        for (std::size_t f = 0; f < poses.size(); ++f)
        {
            const auto predicted = predict(p, transforms[f], w);
            for (std::size_t v = 0; v < n; ++v)
                error += (predicted[v] - poses[f][v]).length2();
        }
        result.mError = static_cast<float>(std::sqrt(error / (n * poses.size())));
        return result;
    }
}
#endif
