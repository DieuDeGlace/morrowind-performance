#ifndef OPENMW_MWPHYSICS_ITEMPHYSICS_HPP
#define OPENMW_MWPHYSICS_ITEMPHYSICS_HPP

#include <components/vfs/pathutil.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../mwworld/ptr.hpp"
#include <osg/Quat>
#include <osg/Vec3f>

namespace Resource
{
    class ResourceSystem;
}

namespace MWPhysics
{
    class PhysicsSystem;
    class RagdollWorld;

    // One lazily-created body per interacted-with item. Untouched clutter is
    // neither scanned every frame nor registered in the dynamics solver.
    class ItemPhysics
    {
    public:
        ItemPhysics(PhysicsSystem& physics, Resource::ResourceSystem* resources);
        ~ItemPhysics();

        bool enabled();
        bool canGrab(const MWWorld::Ptr& item);
        bool applyImpulse(const MWWorld::Ptr& item, const osg::Vec3f& impulse, const MWWorld::Ptr& culprit);
        bool grab(const MWWorld::Ptr& player, const MWWorld::Ptr& item, const osg::Vec3f& point, const osg::Vec3f& eye,
            const osg::Quat& view, float reach);
        bool throwHeld(const MWWorld::Ptr& player, const osg::Quat& view);
        bool hit(const MWWorld::Ptr& player, const MWWorld::Ptr& target, const osg::Vec3f& point, const osg::Vec3f& eye,
            const osg::Vec3f& direction, float reach, float strength);

        void prepare(float dt);
        void synchronize();
        void remove(const MWWorld::Ptr& item);
        void externalTransform(const MWWorld::Ptr& item);
        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated);

    private:
        class Body;
        std::shared_ptr<Body> getOrCreate(const MWWorld::Ptr& item);
        bool fracture(const std::shared_ptr<Body>& body);
        void buildDebrisIndex();
        PhysicsSystem& mPhysics;
        Resource::ResourceSystem* mResources;
        std::unordered_map<std::string, std::vector<VFS::Path::Normalized>> mDebris;
        std::unordered_map<std::string, ESM::RefId> mDebrisRecords;
        std::deque<MWWorld::Ptr> mDebrisPieces;
        bool mDebrisIndexed = false;
        std::unordered_map<const MWWorld::LiveCellRefBase*, std::shared_ptr<Body>> mBodies;
        bool mSynchronizing = false;
        bool mCompatibilityChecked = false;
        bool mEnabled = true;
        std::uint64_t mSerial = 0;
    };
}

#endif
