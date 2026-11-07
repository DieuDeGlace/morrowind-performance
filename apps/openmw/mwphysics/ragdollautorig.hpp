#ifndef OPENMW_MWPHYSICS_RAGDOLLAUTORIG_HPP
#define OPENMW_MWPHYSICS_RAGDOLLAUTORIG_HPP

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osgAnimation/Bone>
#include <osgAnimation/RigGeometry>
#include <osgAnimation/Skeleton>

#include <components/misc/strings/algorithm.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/riggeometryosgaextension.hpp>
#include <components/sceneutil/skeleton.hpp>

namespace MWPhysics
{
    // Keep a frame outside the Skeleton: skinned drawables deliberately ignore
    // transforms below their Skeleton. This also supports models with no bones.
    class RagdollModelFrame
    {
    public:
        explicit RagdollModelFrame(osg::Group& model)
            : mModel(&model)
        {
            if (model.getNumParents() != 1)
                return;
            osg::Group* parent = model.getParent(0);
            mFrame = new osg::MatrixTransform;
            mFrame->setName("Ragdoll model frame");
            if (!parent->replaceChild(&model, mFrame))
            {
                mFrame = nullptr;
                return;
            }
            mFrame->addChild(&model);
        }

        ~RagdollModelFrame()
        {
            if (!mFrame)
                return;
            if (mFrame->getNumParents() == 1)
                mFrame->getParent(0)->replaceChild(mFrame, mModel);
            mFrame->removeChild(mModel);
        }

        RagdollModelFrame(const RagdollModelFrame&) = delete;
        RagdollModelFrame& operator=(const RagdollModelFrame&) = delete;
        osg::MatrixTransform* get() const { return mFrame.get(); }

    private:
        osg::ref_ptr<osg::Group> mModel;
        osg::ref_ptr<osg::MatrixTransform> mFrame;
    };

    struct AutoRagdollPart
    {
        osg::ref_ptr<osg::MatrixTransform> mNode;
        std::size_t mParent = 0;
        osg::Vec3f mStart;
        osg::Vec3f mEnd;
        float mRadius = 1.f;
        float mMass = 1.f;
    };

    struct AutoRagdoll
    {
        std::vector<AutoRagdollPart> mParts;
        std::vector<osg::ref_ptr<osg::Node>> mAnimatedNodes;
    };

    namespace RagdollDetail
    {
        constexpr std::size_t NoParent = std::numeric_limits<std::size_t>::max();

        inline bool finite(const osg::Vec3f& value)
        {
            return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
        }

        inline void expand(osg::BoundingBoxf& box, const osg::BoundingBoxf& source, const osg::Matrixf& matrix)
        {
            if (!source.valid())
                return;
            for (unsigned int corner = 0; corner < 8; ++corner)
            {
                const osg::Vec3f point = source.corner(corner) * matrix;
                if (finite(point))
                    box.expandBy(point);
            }
        }

        struct Bone
        {
            osg::MatrixTransform* mNode;
            std::size_t mParent;
            osg::Matrixf mWorld;
            osg::NodePath mPath;
            osg::BoundingBoxf mBounds;
            bool mUsed = false;
        };

        // Only real skin influences, explicit OSG bones and geometry-bearing
        // transforms are candidates. Particle emitters and collision markers
        // must not become limbs just because they have animation controllers.
        class CollectBones : public osg::NodeVisitor
        {
        public:
            explicit CollectBones(const osg::Matrixf& world)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mWorld(world)
            {
            }

            void apply(osg::MatrixTransform& node) override
            {
                const osg::NodePath& path = getNodePath();
                const std::size_t parent = nearest(path, false);
                const std::size_t index = mBones.size();
                mIndices.emplace(&node, index);
                mNames[scope(getNodePath())].emplace(node.getName(), index);
                mBones.push_back({ &node, parent, osg::computeLocalToWorld(path) * mWorld, path, {},
                    dynamic_cast<osgAnimation::Bone*>(&node) != nullptr });
                traverse(node);
            }

            void apply(osg::Drawable& drawable) override
            {
                if (auto* rig = dynamic_cast<SceneUtil::RigGeometry*>(&drawable))
                {
                    for (const auto& bone : rig->getBoneInfo())
                        mSkinBones.emplace_back(scope(getNodePath()), bone);
                    return;
                }

                if (auto* holder = dynamic_cast<SceneUtil::RigGeometryHolder*>(&drawable))
                {
                    if (auto rig = holder->getSourceRigGeometry())
                        expand(mBounds, rig->getBoundingBox(), osg::computeLocalToWorld(getNodePath()) * mWorld);
                    return;
                }

                const std::size_t owner = nearest(getNodePath(), true);
                if (owner == NoParent)
                    return;

                osg::BoundingBoxf poseBounds;
                const osg::BoundingBoxf* bounds = nullptr;
                if (auto* morph = dynamic_cast<SceneUtil::MorphGeometry*>(&drawable))
                {
                    const auto& targets = morph->getMorphTargetList();
                    if (!targets.empty() && targets[0].getOffsets())
                    {
                        const auto& vertices = *targets[0].getOffsets();
                        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
                        {
                            osg::Vec3f point = vertices[vertex];
                            for (std::size_t target = 1; target < targets.size(); ++target)
                                if (targets[target].getOffsets() && vertex < targets[target].getOffsets()->size())
                                    point += (*targets[target].getOffsets())[vertex] * targets[target].getWeight();
                            if (finite(point))
                                poseBounds.expandBy(point);
                        }
                    }
                    else if (morph->getSourceGeometry())
                        poseBounds = morph->getSourceGeometry()->getBoundingBox();
                    bounds = &poseBounds;
                    mAnimatedNodes.emplace_back(morph);
                }
                else if (auto* geometry = drawable.asGeometry())
                {
                    bounds = &geometry->getBoundingBox();
                    // OSG skinning has explicit Bone nodes, rather than the
                    // SceneUtil influence list used by NIFs.
                    if (dynamic_cast<osgAnimation::RigGeometry*>(geometry))
                    {
                        expand(mBounds, *bounds, osg::computeLocalToWorld(getNodePath()) * mWorld);
                        return;
                    }
                }
                if (!bounds || !bounds->valid())
                    return;

                Bone& bone = mBones[owner];
                const osg::Matrixf world = osg::computeLocalToWorld(getNodePath()) * mWorld;
                expand(bone.mBounds, *bounds, world);
                expand(mBounds, *bounds, world);
                bone.mUsed = true;
                rememberPath(bone.mPath);
            }

            void finish()
            {
                for (const auto& [skeleton, skin] : mSkinBones)
                {
                    const auto names = mNames.find(skeleton);
                    if (names == mNames.end())
                        continue;
                    const auto found = names->second.find(skin.mName);
                    if (found == names->second.end())
                        continue;
                    Bone& bone = mBones[found->second];
                    bone.mUsed = true;
                    if (skin.mBoundSphere.valid())
                    {
                        osg::BoundingBoxf box;
                        box.expandBy(skin.mBoundSphere);
                        expand(bone.mBounds, box, bone.mWorld);
                        expand(mBounds, box, bone.mWorld);
                    }
                    rememberPath(bone.mPath);
                }
                for (const Bone& bone : mBones)
                    if (bone.mUsed)
                        rememberPath(bone.mPath);
            }

            std::vector<Bone> mBones;
            osg::BoundingBoxf mBounds;
            std::vector<osg::ref_ptr<osg::Node>> mAnimatedNodes;

        private:
            const osg::Node* scope(const osg::NodePath& path) const
            {
                for (auto it = path.rbegin(); it != path.rend(); ++it)
                    if (dynamic_cast<SceneUtil::Skeleton*>(*it) || dynamic_cast<osgAnimation::Skeleton*>(*it))
                        return *it;
                return nullptr;
            }

            std::size_t nearest(const osg::NodePath& path, bool includeLast) const
            {
                std::size_t end = path.size() - (includeLast ? 0 : 1);
                while (end > 0)
                {
                    const auto found = mIndices.find(path[--end]);
                    if (found != mIndices.end())
                        return found->second;
                }
                return NoParent;
            }

            void rememberPath(const osg::NodePath& path)
            {
                for (osg::Node* node : path)
                    if (mRemembered.insert(node).second)
                        mAnimatedNodes.emplace_back(node);
            }

            osg::Matrixf mWorld;
            std::unordered_map<const osg::Node*, std::size_t> mIndices;
            using Names
                = std::unordered_map<std::string, std::size_t, Misc::StringUtils::CiHash, Misc::StringUtils::CiEqual>;
            std::unordered_map<const osg::Node*, Names> mNames;
            std::vector<std::pair<const osg::Node*, SceneUtil::RigGeometry::BoneInfo>> mSkinBones;
            std::unordered_set<const osg::Node*> mRemembered;
        };

        inline void fitBounds(AutoRagdollPart& part, const osg::BoundingBoxf& box)
        {
            const osg::Vec3f extent = box._max - box._min;
            unsigned int axis = 0;
            if (extent.y() > extent[axis])
                axis = 1;
            if (extent.z() > extent[axis])
                axis = 2;
            part.mStart = part.mEnd = box.center();
            part.mStart[axis] = box._min[axis];
            part.mEnd[axis] = box._max[axis];
            part.mRadius = std::max(.05f, .5f * std::max(extent[(axis + 1) % 3], extent[(axis + 2) % 3]));
        }
    }

    // Bounds and pivots are evaluated in the actual death pose, including
    // reference scale. No record IDs or bone naming convention are required.
    inline AutoRagdoll makeAutoRagdoll(osg::MatrixTransform& frame, const osg::Matrixf& world, std::size_t budget = 24)
    {
        using namespace RagdollDetail;
        CollectBones collected(world);
        frame.accept(collected);
        collected.finish();
        AutoRagdoll result;
        if (budget == 0 || !collected.mBounds.valid() || collected.mBones.empty())
            return result;
        auto& bones = collected.mBones;
        const float span = (collected.mBounds._max - collected.mBounds._min).length();
        if (!std::isfinite(span) || span < .01f || span > 100000.f)
            return result;
        const float minimum = std::max(.05f, span * .005f);

        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < bones.size(); ++i)
            if (bones[i].mUsed && finite(bones[i].mWorld.getTrans()))
                candidates.push_back(i);
        if (candidates.empty())
            return result;

        // The common ancestor joins separate limbs. Exclude motion roots above
        // it: their origins are often at ground level, outside the animal.
        std::size_t root = candidates.front();
        for (std::size_t candidate : candidates)
        {
            std::unordered_set<std::size_t> ancestors;
            for (std::size_t i = candidate; i != NoParent; i = bones[i].mParent)
                ancestors.insert(i);
            while (root != NoParent && !ancestors.contains(root))
                root = bones[root].mParent;
        }
        if (root == NoParent)
            return result;

        std::vector<float> importance(bones.size(), 0.f);
        std::vector<std::size_t> meaningful{ root };
        for (std::size_t i : candidates)
        {
            if (i == root)
                continue;
            std::size_t parent = bones[i].mParent;
            while (parent != NoParent && parent != root && !bones[parent].mUsed)
                parent = bones[parent].mParent;
            if (parent == NoParent)
                continue;
            const float distance = (bones[i].mWorld.getTrans() - bones[parent].mWorld.getTrans()).length();
            // Coincident mesh/shadow transforms are not separate joints.
            // Collapsing them is essential for vertex-morphed creatures.
            if (distance < minimum)
                continue;
            importance[i] = distance + (bones[i].mBounds.valid() ? bones[i].mBounds.radius() : 0.f);
            meaningful.push_back(i);
        }

        // Retain the largest segments rather than the first N bones, which
        // could otherwise spend the whole budget on one hand or facial rig.
        if (meaningful.size() > budget)
        {
            std::stable_sort(meaningful.begin() + 1, meaningful.end(),
                [&](std::size_t a, std::size_t b) { return importance[a] > importance[b]; });
            meaningful.resize(budget);
        }
        std::sort(meaningful.begin(), meaningful.end()); // Scene traversal puts ancestors first.
        if (meaningful.size() == 1)
        {
            AutoRagdollPart part;
            part.mNode = &frame;
            fitBounds(part, collected.mBounds);
            part.mMass = std::clamp(
                part.mRadius * part.mRadius * std::max((part.mEnd - part.mStart).length(), 2.f * part.mRadius) * .002f,
                .2f, 80.f);
            result.mParts.push_back(std::move(part));
        }
        else
        {
            std::unordered_map<std::size_t, std::size_t> selected;
            for (std::size_t i : meaningful)
            {
                AutoRagdollPart part;
                part.mNode = bones[i].mNode;
                part.mStart = part.mEnd = bones[i].mWorld.getTrans();
                std::size_t parent = bones[i].mParent;
                while (parent != NoParent && !selected.contains(parent))
                    parent = bones[parent].mParent;
                part.mParent = parent == NoParent ? 0 : selected.at(parent);
                selected.emplace(i, result.mParts.size());
                result.mParts.push_back(std::move(part));
            }
            for (std::size_t index = 0; index < meaningful.size(); ++index)
            {
                AutoRagdollPart& part = result.mParts[index];
                const Bone& bone = bones[meaningful[index]];
                float best = 0.f;
                for (std::size_t child = index + 1; child < result.mParts.size(); ++child)
                {
                    if (result.mParts[child].mParent != index)
                        continue;
                    const float distance = (result.mParts[child].mStart - part.mStart).length();
                    if (distance > best)
                    {
                        best = distance;
                        part.mEnd = result.mParts[child].mStart;
                    }
                }
                if (best < minimum && bone.mBounds.valid())
                    fitBounds(part, bone.mBounds);
                else
                    part.mRadius
                        = std::max(best * .18f, bone.mBounds.valid() ? bone.mBounds.radius() * .3f : span * .035f);
                part.mRadius = std::clamp(part.mRadius, std::max(.05f, span * .006f), std::max(.05f, span * .15f));
                part.mMass = std::clamp(part.mRadius * part.mRadius
                        * std::max((part.mEnd - part.mStart).length(), 2.f * part.mRadius) * .002f,
                    .2f, 60.f);
            }
        }
        float largestMass = 0.f;
        for (const auto& part : result.mParts)
            largestMass = std::max(largestMass, part.mMass);
        for (auto& part : result.mParts)
            part.mMass = std::max(part.mMass, largestMass / 12.f);
        result.mAnimatedNodes = std::move(collected.mAnimatedNodes);
        return result;
    }
}

#endif
