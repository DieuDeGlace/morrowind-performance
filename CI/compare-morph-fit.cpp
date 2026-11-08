#include "apps/openmw/mwphysics/ragdollmorphfit.hpp"
#include "morphfit-baseline.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>

namespace
{
    std::size_t allocations = 0;
}

void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* memory = std::malloc(std::max<std::size_t>(1, size)))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    struct Sample
    {
        double milliseconds;
        std::size_t allocations;
    };
    template <class Function, class Layout>
    Sample measure(Function function, Layout& layout)
    {
        const auto before = allocations;
        const auto start = std::chrono::steady_clock::now();
        layout = function();
        const auto end = std::chrono::steady_clock::now();
        return { std::chrono::duration<double, std::milli>(end - start).count(), allocations - before };
    }
    template <class A, class B>
    bool identical(const A& a, const B& b)
    {
        return !a.mPivots.empty() && a.mRest == b.mRest && a.mWeights == b.mWeights
            && a.mPivots == b.mPivots && a.mParents == b.mParents && a.mLowTip == b.mLowTip
            && a.mHighTip == b.mHighTip && a.mAxis == b.mAxis && a.mError == b.mError;
    }
    void compare(int rings, int frames)
    {
        MWPhysics::MorphFit::Pose rest;
        for (int y = 0; y < rings; ++y)
            for (int i = 0; i < 8; ++i)
            {
                const float angle = i * 6.2831853f / 8;
                rest.emplace_back(3.f * std::cos(angle), y * 72.f / (rings - 1) - 36.f, 3.f * std::sin(angle));
            }
        std::vector<MWPhysics::MorphFit::Pose> poses;
        for (int frame = 0; frame < frames; ++frame)
        {
            auto pose = rest;
            for (auto& v : pose)
            {
                const float phase = frame * 5.6f / (frames - 1);
                v.z() += 8.f * std::sin(v.y() * .06f + phase);
                v.y() *= 1.f + .15f * std::sin(phase);
            }
            poses.push_back(std::move(pose));
        }
        const auto beforeFit = [&] { return BaselineMorphFit::fit(rest, poses); };
        const auto afterFit = [&] { return MWPhysics::MorphFit::fit(rest, poses); };
        auto baseline = beforeFit();
        auto optimized = afterFit();
        if (!identical(baseline, optimized))
            throw std::runtime_error("Warmup layouts differ");
        std::array<double, 3> beforeTimes, afterTimes;
        std::size_t beforeAllocations = 0, afterAllocations = 0;
        for (std::size_t i = 0; i < beforeTimes.size(); ++i)
        {
            const auto before = measure(beforeFit, baseline);
            const auto after = measure(afterFit, optimized);
            if (!identical(baseline, optimized))
                throw std::runtime_error("Fitted weights, pivots or error differ");
            beforeTimes[i] = before.milliseconds;
            afterTimes[i] = after.milliseconds;
            beforeAllocations = before.allocations;
            afterAllocations = after.allocations;
        }
        std::sort(beforeTimes.begin(), beforeTimes.end());
        std::sort(afterTimes.begin(), afterTimes.end());
        std::cout << "{\"vertices\":" << rest.size() << ",\"poses\":" << poses.size()
                  << ",\"layout_identical\":true,\"before_allocations\":" << beforeAllocations
                  << ",\"after_allocations\":" << afterAllocations
                  << ",\"before_median_ms\":" << beforeTimes[1]
                  << ",\"after_median_ms\":" << afterTimes[1] << "}";
    }
}

int main()
{
    std::cout << std::fixed << std::setprecision(3)
              << "{\"compiler\":\"" << __VERSION__
              << "\",\"flags\":\"-O2, no LTO\",\"scope\":\"uncached first-time morph fitting only\","
                 "\"baseline_commit\":\"7d1310e63cf153f424a7e4003d1753f89d852927\","
                 "\"optimized_commit\":\"84a74e48e1ca086a0d58c5362e921d11dbcb84a9\",\"samples\":[";
    compare(25, 9);
    std::cout << ',';
    compare(64, 64);
    std::cout << "]}\n";
}
