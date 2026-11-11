#ifndef OPENMW_MWPHYSICS_ITEMPHYSICSRULES_HPP
#define OPENMW_MWPHYSICS_ITEMPHYSICSRULES_HPP

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <components/esm/defs.hpp>
#include <osg/Vec3f>

namespace MWPhysics::ItemPhysicsRules
{
    // Behavioural defaults from MaxYari/OpenMWLuaPhysics 1.4. This is a
    // native implementation; no Lua solver code or third-party assets are bundled.
    constexpr float UnitsPerMetre = 69.99f;
    constexpr float Gravity = 9.8f * UnitsPerMetre;
    constexpr float ThrowImpulse = 500.f;
    constexpr float DragImpulse = 300.f;
    constexpr float SleepSpeed = 7.f;
    constexpr float SleepTime = 1.f;
    constexpr float OffensiveDistance = 4.f * UnitsPerMetre;
    constexpr unsigned MinorOffenseLimit = 30;

    inline bool eligibleType(int type)
    {
        switch (type)
        {
            case ESM::REC_CLOT:
            case ESM::REC_ARMO:
            case ESM::REC_BOOK:
            case ESM::REC_INGR:
            case ESM::REC_MISC:
            case ESM::REC_ALCH:
            case ESM::REC_WEAP:
            case ESM::REC_APPA:
            case ESM::REC_LOCK:
            case ESM::REC_PROB:
            case ESM::REC_REPA:
                return true;
            default:
                return false;
        }
    }

    struct Properties
    {
        float mRadius;
        float mMass;
        float mRestitution = .5f;
        float mLinearDamping = .1f;
        float mAngularDamping = .1f;
        float mBuoyancy = .5f;
    };

    inline Properties properties(const osg::Vec3f& halfExtents, bool metal = false)
    {
        const float volume
            = (halfExtents.x() / UnitsPerMetre) * (halfExtents.y() / UnitsPerMetre) * (halfExtents.z() / UnitsPerMetre);
        return { std::max(2.f, std::min({ halfExtents.x(), halfExtents.y(), halfExtents.z() })),
            std::clamp(volume * (metal ? 50.f : 25.f), 1.f, 10000.f) };
    }

    inline int crimeAmount(int value, bool destroyed = false)
    {
        return std::max(25, static_cast<int>(value * (destroyed ? 2.f : .1f)));
    }

    // Optional debris assets use names such as
    // misc_com_bottle_&05&06__blue_chunk_0.nif. Expand the selector, never
    // infer destructibility for models without an installed matching asset.
    inline std::vector<std::string> debrisTargets(std::string_view stem)
    {
        if (stem.starts_with("x"))
            stem.remove_prefix(1);
        stem = stem.substr(0, stem.find("__"));
        const auto selector = stem.find("_&");
        if (selector == std::string_view::npos)
            return { std::string(stem) };
        std::vector<std::string> result;
        const std::string prefix(stem.substr(0, selector + 1));
        std::size_t position = selector + 1;
        while ((position = stem.find('&', position)) != std::string_view::npos)
        {
            const auto first = ++position;
            while (position < stem.size() && stem[position] >= '0' && stem[position] <= '9')
                ++position;
            if (position > first)
                result.push_back(prefix + std::string(stem.substr(first, position - first)));
        }
        return result;
    }

    class SleepTimer
    {
    public:
        bool update(float speed, float dt, bool held)
        {
            if (held || speed >= SleepSpeed)
                mQuiet = 0.f;
            else
                mQuiet += std::clamp(dt, 0.f, .05f);
            return mQuiet >= SleepTime;
        }
        void reset() { mQuiet = 0.f; }

    private:
        float mQuiet = 0.f;
    };

    // The collision proxy is a small sphere, so letting the solver rotate it
    // makes every item roll like a ball: a flat book dragged over terrain or
    // left on a slope tumbles for as long as it keeps moving. As in the Lua
    // mod, tumbling is cosmetic instead: impacts start it, air drag and
    // contact end it, and it never feeds back into the linear motion.
    constexpr float ImpactSpinDamping = .5f;
    constexpr float ImpactSpinGain = 6.f;
    constexpr float SpinDrag = .1f;
    constexpr float SpinToRadians = .01f;
    constexpr float MaxSpin = 5000.f;
    constexpr float ContactSpinHalfLife = 1.f / 60.f;

    class Spin
    {
    public:
        // normal points from the surface towards the item; velocity is the
        // item's velocity after the bounce.
        void impact(const osg::Vec3f& normal, const osg::Vec3f& velocity, float radius, float largestHalfExtent)
        {
            mVelocity *= ImpactSpinDamping;
            if (largestHalfExtent <= 0.f)
                return;
            mVelocity += ((normal * -radius) ^ (-velocity)) * (ImpactSpinGain / largestHalfExtent);
            const float speed = mVelocity.length();
            if (!std::isfinite(speed))
                mVelocity = osg::Vec3f();
            else if (speed > MaxSpin)
                mVelocity *= MaxSpin / speed;
        }

        // Returns the world-space rotation for this interval as axis * radians.
        // grounded is true while the item rests on something or is being held.
        osg::Vec3f advance(float dt, bool grounded)
        {
            dt = std::clamp(dt, 0.f, .05f);
            if (grounded)
                mVelocity *= std::pow(.5f, dt / ContactSpinHalfLife);
            else
                mVelocity *= 1.f - SpinDrag * dt;
            return mVelocity * (dt * SpinToRadians);
        }

        void reset() { mVelocity = osg::Vec3f(); }
        const osg::Vec3f& velocity() const { return mVelocity; }

    private:
        osg::Vec3f mVelocity;
    };
}

#endif
