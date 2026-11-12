#ifndef OPENMW_MWRENDER_SHATTEREFFECT_HPP
#define OPENMW_MWRENDER_SHATTEREFFECT_HPP

#include <deque>
#include <vector>

#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include "shatterrules.hpp"

namespace osg
{
    class Group;
    class PositionAttitudeTransform;
}

namespace MWRender
{
    // Breaks a copy of an object's own model into shards that fly apart,
    // land and shrink away. It needs no debris assets: the shards are the
    // model's triangles, grouped by where they lie.
    class ShatterEffects
    {
    public:
        explicit ShatterEffects(osg::ref_ptr<osg::Group> parent);
        ~ShatterEffects();

        // object is the object's base node, placed at position with attitude and
        // scale. Returns false when its model has no plain triangle geometry.
        bool add(const osg::Group& object, const osg::Vec3f& position, const osg::Quat& attitude,
            const osg::Vec3f& scale, const osg::Vec3f& velocity, float floor);
        void update(float dt);
        void clear();

    private:
        struct Shard
        {
            osg::ref_ptr<osg::PositionAttitudeTransform> mNode;
            ShatterRules::Motion mMotion;
            osg::Vec3f mSpinAxis;
            float mFloor = 0.f;
        };
        struct Effect
        {
            std::vector<Shard> mShards;
            float mAge = 0.f;
        };

        void remove(Effect& effect);

        osg::ref_ptr<osg::Group> mParent;
        std::deque<Effect> mEffects;
    };
}

#endif
