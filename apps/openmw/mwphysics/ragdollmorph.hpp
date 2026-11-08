#ifndef OPENMW_MWPHYSICS_RAGDOLLMORPH_H
#define OPENMW_MWPHYSICS_RAGDOLLMORPH_H

#include <cctype>
#include <deque>
#include <memory>
#include <mutex>

#include <osg/CopyOp>
#include <osg/MatrixTransform>
#include <osg/StateSet>

#include <components/debug/debuglog.hpp>
#include <components/nifosg/controller.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/skeleton.hpp>

#include "ragdollautorig.hpp"
#include "ragdollmorphfit.hpp"

namespace MWPhysics
{
    namespace MorphRagdollDetail
    {
        struct Mesh
        {
            osg::ref_ptr<SceneUtil::MorphGeometry> mMorph;
            osg::Matrixf mMatrix;
            osg::NodePath mPath;
            osg::BoundingBoxf mBounds;
            bool mShadow = false;
        };
        class Gather : public osg::NodeVisitor
        {
        public:
            Gather()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }
            void apply(osg::Drawable& drawable) override
            {
                auto* morph = dynamic_cast<SceneUtil::MorphGeometry*>(&drawable);
                if (!morph || !morph->getSourceGeometry())
                    return;
                const auto& targets = morph->getMorphTargetList();
                if (targets.size() < 2 || !targets[0].getOffsets() || targets[0].getOffsets()->size() < 24
                    || targets[0].getOffsets()->size() > MorphFit::MaxVertices)
                    return;
                Mesh mesh{ morph, osg::computeLocalToWorld(getNodePath()), getNodePath(), {}, false };
                for (const auto& point : *targets[0].getOffsets())
                {
                    const auto transformed = point * mesh.mMatrix;
                    if (!MorphFit::finite(transformed))
                        return;
                    mesh.mBounds.expandBy(transformed);
                }
                std::string name = morph->getName();
                for (char& c : name)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                mesh.mShadow = name.find("shadow") != std::string::npos;
                mMeshes.push_back(std::move(mesh));
            }
            std::vector<Mesh> mMeshes;
        };
        inline MorphFit::Pose pose(const SceneUtil::MorphGeometry& morph, const std::vector<float>* weights = nullptr)
        {
            const auto& targets = morph.getMorphTargetList();
            if (targets.empty() || !targets[0].getOffsets())
                return {};
            MorphFit::Pose result(targets[0].getOffsets()->begin(), targets[0].getOffsets()->end());
            for (std::size_t i = 1; i < targets.size(); ++i)
            {
                const auto* offsets = targets[i].getOffsets();
                if (!offsets || offsets->size() != result.size())
                    return {};
                const float w = weights ? (i < weights->size() ? (*weights)[i] : 0.f) : targets[i].getWeight();
                if (!std::isfinite(w))
                    return {};
                if (w == 0.f)
                    continue;
                for (std::size_t v = 0; v < result.size(); ++v)
                    result[v] += (*offsets)[v] * w;
            }
            for (const auto& point : result)
                if (!MorphFit::finite(point))
                    return {};
            return result;
        }
        inline std::shared_ptr<const MorphFit::Layout> layout(SceneUtil::MorphGeometry& morph)
        {
            struct Cached
            {
                std::vector<osg::ref_ptr<const osg::Vec3Array>> mTargets;
                std::shared_ptr<const MorphFit::Layout> mLayout;
            };
            static std::mutex mutex;
            static std::deque<Cached> cache;
            std::lock_guard lock(mutex);
            std::vector<osg::ref_ptr<const osg::Vec3Array>> targets;
            for (const auto& target : morph.getMorphTargetList())
                targets.emplace_back(target.getOffsets());
            for (const auto& entry : cache)
                if (entry.mTargets == targets)
                    return entry.mLayout;
            std::vector<std::vector<float>> samples;
            for (auto* callback = morph.getUpdateCallback(); callback; callback = callback->getNestedCallback())
                if (auto* ctrl = dynamic_cast<NifOsg::GeomMorpherController*>(callback))
                {
                    samples = ctrl->getMorphSamples();
                    break;
                }
            // Unsupported controller formats retain their original whole/rigid
            // part handling. Never infer anatomy from particle/effect markers.
            if (samples.empty() || targets.empty() || !targets.front())
                return {};
            std::vector<MorphFit::Pose> poses;
            for (const auto& sample : samples)
                poses.push_back(pose(morph, &sample));
            auto result = std::make_shared<MorphFit::Layout>(
                MorphFit::fit(MorphFit::Pose(targets.front()->begin(), targets.front()->end()), poses));
            if (cache.size() >= 64)
                cache.pop_front();
            cache.push_back({ std::move(targets), result });
            return result;
        }
        inline float volume(const osg::BoundingBoxf& box)
        {
            const auto size = box._max - box._min;
            return std::max(0.f, size.x() * size.y() * size.z());
        }
    }
    // Owns only death-time, per-instance replacements. Source arrays, animation
    // controllers, installed NIFs and every living actor remain untouched.
    class RagdollMorphRig
    {
    public:
        explicit RagdollMorphRig(osg::MatrixTransform& frame)
            : mFrame(&frame)
        {
            using namespace MorphRagdollDetail;
            Gather gathered;
            frame.accept(gathered);
            if (gathered.mMeshes.empty())
                return;
            RagdollDetail::CollectBones collected(osg::Matrixf::identity());
            frame.accept(collected);
            collected.finish();
            const auto actorBounds = collected.mLocalBounds;
            if (!actorBounds.valid())
                return;
            const float span = (actorBounds._max - actorBounds._min).length();
            if (!std::isfinite(span) || span < .01f || span > 100000.f)
                return;
            const float actorVolume = volume(actorBounds);
            std::stable_sort(gathered.mMeshes.begin(), gathered.mMeshes.end(),
                [](const auto& a, const auto& b) { return volume(a.mBounds) > volume(b.mBounds); });
            for (const auto& originalMesh : gathered.mMeshes)
            {
                auto mesh = originalMesh;
                if (mBodies + 5 > 16)
                    break;
                if (mesh.mShadow)
                    continue;
                if ((mesh.mBounds._max - mesh.mBounds._min).length() < span * .42f
                    || volume(mesh.mBounds) < actorVolume * .02f)
                    continue;
                const auto fitted = layout(*mesh.mMorph);
                if (!fitted || fitted->mPivots.empty())
                    continue;
                const auto frozen = pose(*mesh.mMorph);
                if (frozen.size() != fitted->mRest.size())
                    continue;
                auto transforms = MorphFit::solve(fitted->mRest, frozen, fitted->mWeights, 8);
                const std::size_t count = fitted->mPivots.size();
                if (mBodies + count + 2 > 16)
                    continue;
                std::vector<osg::Matrixf> matrices;
                std::vector<osg::ref_ptr<osg::MatrixTransform>> nodes;
                for (std::size_t b = 0; b < count; ++b)
                {
                    const auto pivot = transforms[b].apply(fitted->mPivots[b]);
                    matrices.push_back(
                        osg::Matrixf::rotate(transforms[b].mRotation) * osg::Matrixf::translate(pivot) * mesh.mMatrix);
                    nodes.emplace_back(new osg::MatrixTransform);
                }
                osg::ref_ptr<osg::Group> anchor = &frame;
                osg::Matrixf anchorWorld;
                osg::NodePath anchorPath;
                for (auto* node : mesh.mPath)
                {
                    anchorPath.push_back(node);
                    if (auto* transform = dynamic_cast<osg::MatrixTransform*>(node))
                    {
                        anchor = transform;
                        anchorWorld = osg::computeLocalToWorld(anchorPath);
                    }
                }
                const auto inverseAnchor = osg::Matrixf::inverse(anchorWorld);
                mesh.mMatrix *= inverseAnchor;
                for (auto& matrix : matrices)
                    matrix *= inverseAnchor;
                osg::ref_ptr<SceneUtil::Skeleton> skeleton = new SceneUtil::Skeleton;
                const std::size_t root = (count - 1) / 2;
                for (std::size_t b = 0; b < count; ++b)
                {
                    nodes[b]->setName("Ragdoll morph " + std::to_string(mBodies) + " part " + std::to_string(b));
                    nodes[b]->setUserValue("ragdoll.morph", true);
                    const auto parent = fitted->mParents[b];
                    nodes[b]->setMatrix(
                        b == root ? matrices[b] : matrices[b] * osg::Matrixf::inverse(matrices[parent]));
                    if (b == root)
                        skeleton->addChild(nodes[b]);
                    else
                        nodes[parent]->addChild(nodes[b]);
                }
                for (int end = 0; end < 2; ++end)
                {
                    const auto b = end == 0 ? 0 : count - 1;
                    const auto tip = transforms[b].apply(end == 0 ? fitted->mLowTip : fitted->mHighTip) * mesh.mMatrix;
                    const auto matrix = osg::Matrixf::translate(tip);
                    osg::ref_ptr<osg::MatrixTransform> node = new osg::MatrixTransform;
                    node->setName("Ragdoll morph " + std::to_string(mBodies) + " endpoint " + std::to_string(end));
                    node->setUserValue("ragdoll.morph", true);
                    node->setMatrix(matrix * osg::Matrixf::inverse(matrices[b]));
                    nodes[b]->addChild(node);
                    nodes.push_back(node);
                    matrices.push_back(matrix);
                }
                std::vector<SceneUtil::RigGeometry::BoneInfo> info;
                for (std::size_t b = 0; b < matrices.size(); ++b)
                {
                    const auto inverse = osg::Matrixf::inverse(matrices[b]);
                    osg::BoundingBoxf bounds;
                    if (b < count)
                        for (std::size_t v = 0; v < frozen.size(); ++v)
                            if (fitted->mWeights[v][b] > .002f)
                                bounds.expandBy(frozen[v] * mesh.mMatrix * inverse);
                    if (!bounds.valid())
                        bounds.set(osg::Vec3f(-.02f, -.02f, -.02f), osg::Vec3f(.02f, .02f, .02f));
                    info.push_back({ nodes[b]->getName(), osg::BoundingSpheref(bounds), inverse });
                }
                replace(*skeleton, mesh, fitted->mWeights, info);
                for (const auto& originalShadow : gathered.mMeshes)
                {
                    auto shadow = originalShadow;
                    if (!shadow.mShadow || wasReplaced(*shadow.mMorph))
                        continue;
                    const float distance = (shadow.mBounds.center() - mesh.mBounds.center()).length();
                    if (distance > span * .12f)
                        continue;
                    shadow.mMatrix *= inverseAnchor;
                    const auto& base = *shadow.mMorph->getMorphTargetList()[0].getOffsets();
                    MorphFit::Weights weights(base.size(), std::vector<float>(count));
                    const auto samples = std::min(MorphFit::MaxTrainingVertices, fitted->mRest.size());
                    std::vector<std::pair<osg::Vec3f, std::size_t>> sampledRest;
                    for (std::size_t sample = 0; sample < samples; ++sample)
                    {
                        const auto i = sample * (fitted->mRest.size() - 1) / (samples - 1);
                        sampledRest.emplace_back(fitted->mRest[i] * mesh.mMatrix, i);
                    }
                    for (std::size_t v = 0; v < base.size(); ++v)
                    {
                        const auto point = base[v] * shadow.mMatrix;
                        std::array<std::pair<float, std::size_t>, 3> near;
                        near.fill({ std::numeric_limits<float>::infinity(), 0 });
                        for (const auto& [position, i] : sampledRest)
                        {
                            const float d = (point - position).length2();
                            if (d < near[2].first)
                            {
                                near[2] = { d, i };
                                std::sort(near.begin(), near.end());
                            }
                        }
                        for (const auto& [d, i] : near)
                            for (std::size_t b = 0; b < count; ++b)
                                weights[v][b] += fitted->mWeights[i][b] / std::max(d, .0001f);
                        std::vector<std::size_t> order(count);
                        std::iota(order.begin(), order.end(), 0);
                        std::sort(
                            order.begin(), order.end(), [&](auto a, auto b) { return weights[v][a] > weights[v][b]; });
                        for (std::size_t i = 3; i < count; ++i)
                            weights[v][order[i]] = 0;
                        const float sum = std::accumulate(weights[v].begin(), weights[v].end(), 0.f);
                        for (auto& value : weights[v])
                            value /= sum;
                    }
                    replace(*skeleton, shadow, weights, info);
                }
                anchor->addChild(skeleton);
                mAttachments.emplace_back(anchor, skeleton);
                mBodies += count + 2;
                Log(Debug::Info) << "Ragdoll morph: " << mesh.mMorph->getName() << " fitted " << count
                                 << " skin regions from original animation (error " << fitted->mError << ")";
            }
        }
        ~RagdollMorphRig() { restore(); }
        RagdollMorphRig(const RagdollMorphRig&) = delete;
        RagdollMorphRig& operator=(const RagdollMorphRig&) = delete;
        bool converted() const { return !mOriginal.empty(); }
        const std::vector<osg::ref_ptr<osg::Node>>& animatedNodes() const { return mAnimatedNodes; }
        void restore()
        {
            if (!mFrame)
                return;
            for (const auto& [anchor, skeleton] : mAttachments)
                anchor->removeChild(skeleton);
            for (const auto& entry : mOriginal)
                entry.first->setNodeMask(entry.second);
            mFrame = nullptr;
        }

    private:
        bool wasReplaced(const SceneUtil::MorphGeometry& morph) const
        {
            return std::ranges::any_of(mOriginal, [&](const auto& entry) { return entry.first.get() == &morph; });
        }
        void replace(SceneUtil::Skeleton& skeleton, const MorphRagdollDetail::Mesh& mesh,
            const MorphFit::Weights& weights, const std::vector<SceneUtil::RigGeometry::BoneInfo>& info)
        {
            const auto frozen = MorphRagdollDetail::pose(*mesh.mMorph);
            if (frozen.size() != weights.size())
                return;
            osg::ref_ptr<osg::Geometry> source
                = new osg::Geometry(*mesh.mMorph->getSourceGeometry(), osg::CopyOp::DEEP_COPY_ARRAYS);
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            for (const auto& point : frozen)
                vertices->push_back(point * mesh.mMatrix);
            source->setVertexArray(vertices);
            if (auto* normals = dynamic_cast<osg::Vec3Array*>(source->getNormalArray()))
            {
                if (normals->size() != vertices->size())
                    source->setNormalArray(nullptr);
                else
                {
                    const auto inverse = osg::Matrixf::inverse(mesh.mMatrix);
                    for (auto& normal : *normals)
                    {
                        normal = osg::Matrixf::transform3x3(inverse, normal);
                        normal.normalize();
                    }
                }
            }
            if (auto* tangents = dynamic_cast<osg::Vec4Array*>(source->getTexCoordArray(7)))
                for (auto& tangent : *tangents)
                {
                    osg::Vec3f t
                        = osg::Matrixf::transform3x3(osg::Vec3f(tangent.x(), tangent.y(), tangent.z()), mesh.mMatrix);
                    t.normalize();
                    tangent.set(t.x(), t.y(), t.z(), tangent.w());
                }
            osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
            rig->setName(mesh.mMorph->getName());
            rig->setNodeMask(mesh.mMorph->getNodeMask());
            osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
            for (auto* node : mesh.mPath)
            {
                if (node->getStateSet())
                    state->merge(*node->getStateSet());
                mAnimatedNodes.emplace_back(node);
            }
            rig->setStateSet(state);
            rig->setSourceGeometry(source);
            auto bones = info;
            rig->setBoneInfo(std::move(bones));
            std::vector<SceneUtil::RigGeometry::BoneWeights> influences(weights.size());
            for (std::size_t v = 0; v < weights.size(); ++v)
                for (std::size_t b = 0; b < weights[v].size(); ++b)
                    if (weights[v][b] > 0)
                        influences[v].emplace_back(b, weights[v][b]);
            rig->setInfluences(influences);
            rig->setTransform(osg::Matrixf::identity());
            skeleton.addChild(rig);
            mOriginal.emplace_back(mesh.mMorph, mesh.mMorph->getNodeMask());
            mesh.mMorph->setNodeMask(0);
        }
        osg::ref_ptr<osg::MatrixTransform> mFrame;
        std::vector<std::pair<osg::ref_ptr<osg::Group>, osg::ref_ptr<SceneUtil::Skeleton>>> mAttachments;
        std::vector<std::pair<osg::ref_ptr<SceneUtil::MorphGeometry>, osg::Node::NodeMask>> mOriginal;
        std::vector<osg::ref_ptr<osg::Node>> mAnimatedNodes;
        std::size_t mBodies = 0;
    };
}
#endif
