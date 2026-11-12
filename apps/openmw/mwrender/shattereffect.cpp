#include "shattereffect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <typeinfo>

#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/PositionAttitudeTransform>
#include <osg/Transform>
#include <osg/TriangleIndexFunctor>

#include <components/sceneutil/lightmanager.hpp>

#include "vismask.hpp"

namespace MWRender
{
    namespace
    {
        constexpr std::size_t MaxEffects = 8;

        struct IndexCollector
        {
            std::vector<unsigned>* mOut = nullptr;
            void operator()(unsigned a, unsigned b, unsigned c)
            {
                mOut->push_back(a);
                mOut->push_back(b);
                mOut->push_back(c);
            }
        };

        bool breakable(const osg::Drawable& drawable)
        {
            // Skinned and morphing drawables have their own update logic.
            return typeid(drawable) == typeid(osg::Geometry)
                && dynamic_cast<const osg::Vec3Array*>(drawable.asGeometry()->getVertexArray()) != nullptr;
        }

        // Lists drawables in traversal order, with their transform relative to
        // the traversal root.
        class DrawableCollector : public osg::NodeVisitor
        {
        public:
            DrawableCollector()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            using osg::NodeVisitor::apply;

            void apply(osg::Drawable& drawable) override
            {
                mDrawables.push_back(&drawable);
                mMatrices.push_back(osg::computeLocalToWorld(getNodePath()));
            }

            // A shard must not share its lighting state with the original.
            void apply(osg::Node& node) override
            {
                for (osg::Callback* callback = node.getCullCallback(); callback;)
                {
                    osg::Callback* next = callback->getNestedCallback();
                    if (dynamic_cast<SceneUtil::LightListCallback*>(callback))
                        mLightCallbacks.emplace_back(&node, callback);
                    callback = next;
                }
                traverse(node);
            }

            std::vector<osg::Drawable*> mDrawables;
            std::vector<osg::Matrix> mMatrices;
            std::vector<std::pair<osg::Node*, osg::Callback*>> mLightCallbacks;
        };

        osg::ref_ptr<osg::PrimitiveSet> makeIndices(const std::vector<unsigned>& indices, unsigned vertexCount)
        {
            if (vertexCount <= 0xffff)
            {
                // GLES2 without OES_element_index_uint only has 16-bit indices.
                osg::ref_ptr<osg::DrawElementsUShort> elements = new osg::DrawElementsUShort(GL_TRIANGLES);
                elements->reserve(indices.size());
                for (unsigned index : indices)
                    elements->push_back(static_cast<unsigned short>(index));
                return elements;
            }
            osg::ref_ptr<osg::DrawElementsUInt> elements = new osg::DrawElementsUInt(GL_TRIANGLES);
            elements->assign(indices.begin(), indices.end());
            return elements;
        }
    }

    ShatterEffects::ShatterEffects(osg::ref_ptr<osg::Group> parent)
        : mParent(std::move(parent))
    {
    }

    ShatterEffects::~ShatterEffects()
    {
        clear();
    }

    bool ShatterEffects::add(const osg::PositionAttitudeTransform& object, const osg::Vec3f& velocity, float floor)
    {
        DrawableCollector source;
        for (unsigned int child = 0; child < object.getNumChildren(); ++child)
            const_cast<osg::Node*>(object.getChild(child))->accept(source);

        // Triangles of every breakable drawable, in model space.
        struct Triangle
        {
            std::size_t mDrawable;
            std::array<unsigned, 3> mIndices;
        };
        std::vector<Triangle> triangles;
        std::vector<osg::Vec3f> centroids;
        for (std::size_t index = 0; index < source.mDrawables.size(); ++index)
        {
            osg::Drawable& drawable = *source.mDrawables[index];
            if (!breakable(drawable))
                continue;
            const auto& vertices = static_cast<const osg::Vec3Array&>(*drawable.asGeometry()->getVertexArray());
            osg::TriangleIndexFunctor<IndexCollector> functor;
            std::vector<unsigned> indices;
            functor.mOut = &indices;
            drawable.accept(functor);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size()
                    || indices[i + 2] >= vertices.size())
                    continue;
                triangles.push_back({ index, { indices[i], indices[i + 1], indices[i + 2] } });
                centroids.push_back((vertices[indices[i]] + vertices[indices[i + 1]] + vertices[indices[i + 2]]) / 3.f
                    * source.mMatrices[index]);
            }
        }
        if (triangles.empty())
            return false;

        const auto seeds = ShatterRules::spreadSeeds(centroids, ShatterRules::shardCount(triangles.size()));
        const std::size_t count = seeds.size();
        // indices[shard][drawable] lists that shard's triangles of that drawable.
        std::vector<std::vector<std::vector<unsigned>>> indices(
            count, std::vector<std::vector<unsigned>>(source.mDrawables.size()));
        std::vector<osg::Vec3f> centres(count);
        std::vector<unsigned> sizes(count);
        for (std::size_t i = 0; i < triangles.size(); ++i)
        {
            const std::size_t shard = ShatterRules::nearestSeed(centroids[i], centroids, seeds);
            auto& list = indices[shard][triangles[i].mDrawable];
            list.insert(list.end(), triangles[i].mIndices.begin(), triangles[i].mIndices.end());
            centres[shard] += centroids[i];
            ++sizes[shard];
        }

        const float scale = object.getScale().x();
        const osg::Matrix placement = osg::Matrix::scale(object.getScale()) * osg::Matrix::rotate(object.getAttitude());
        osg::Vec3f objectCentre;
        for (const auto& centroid : centroids)
            objectCentre += centroid;
        objectCentre /= static_cast<float>(centroids.size());
        const osg::Vec3f worldCentre = objectCentre * placement + object.getPosition();

        Effect effect;
        for (std::size_t shard = 0; shard < count; ++shard)
        {
            if (sizes[shard] == 0)
                continue;
            const osg::Vec3f centre = centres[shard] / static_cast<float>(sizes[shard]);

            osg::ref_ptr<osg::MatrixTransform> model
                = new osg::MatrixTransform(osg::Matrix::translate(-centre) * placement);
            for (unsigned int child = 0; child < object.getNumChildren(); ++child)
                model->addChild(static_cast<osg::Node*>(
                    object.getChild(child)->clone(osg::CopyOp::DEEP_COPY_NODES | osg::CopyOp::DEEP_COPY_DRAWABLES)));
            DrawableCollector copy;
            for (unsigned int child = 0; child < model->getNumChildren(); ++child)
                model->getChild(child)->accept(copy);
            if (copy.mDrawables.size() != source.mDrawables.size())
                return false;
            for (const auto& [node, callback] : copy.mLightCallbacks)
                node->removeCullCallback(callback);
            for (std::size_t index = 0; index < copy.mDrawables.size(); ++index)
            {
                osg::Drawable& drawable = *copy.mDrawables[index];
                if (!breakable(drawable) || indices[shard][index].empty())
                {
                    drawable.setNodeMask(0);
                    continue;
                }
                osg::Geometry& geometry = *drawable.asGeometry();
                geometry.removePrimitiveSet(0, geometry.getNumPrimitiveSets());
                geometry.addPrimitiveSet(
                    makeIndices(indices[shard][index], geometry.getVertexArray()->getNumElements()));
                geometry.dirtyBound();
            }

            Shard piece;
            piece.mNode = new osg::PositionAttitudeTransform;
            piece.mNode->setNodeMask(Mask_Effect);
            piece.mNode->addChild(model);
            piece.mNode->addCullCallback(new SceneUtil::LightListCallback);
            const osg::Vec3f position = centre * placement + object.getPosition();
            piece.mNode->setPosition(position);
            osg::Vec3f outward = position - worldCentre;
            outward.z() = 0.f;
            if (outward.length2() < 1e-4f)
                outward = osg::Vec3f(std::cos(static_cast<float>(shard)), std::sin(static_cast<float>(shard)), 0.f);
            outward.normalize();
            piece.mMotion.mPosition = position;
            piece.mMotion.mVelocity = velocity * .5f + outward * 90.f + osg::Vec3f(0, 0, 70.f + 15.f * (shard % 3));
            piece.mMotion.mSpin = 6.f + 2.f * static_cast<float>(shard % 4);
            piece.mSpinAxis = outward ^ osg::Vec3f(0, 0, 1);
            piece.mSpinAxis.normalize();
            piece.mFloor = floor + std::max(.5f, scale);
            effect.mShards.push_back(std::move(piece));
        }
        if (effect.mShards.empty())
            return false;
        for (const auto& shard : effect.mShards)
            mParent->addChild(shard.mNode);
        while (mEffects.size() >= MaxEffects)
        {
            remove(mEffects.front());
            mEffects.pop_front();
        }
        mEffects.push_back(std::move(effect));
        return true;
    }

    void ShatterEffects::update(float dt)
    {
        for (auto& effect : mEffects)
        {
            effect.mAge += dt;
            const float scale = ShatterRules::scaleAt(effect.mAge);
            for (auto& shard : effect.mShards)
            {
                ShatterRules::step(shard.mMotion, shard.mFloor, dt);
                shard.mNode->setPosition(shard.mMotion.mPosition);
                if (shard.mMotion.mSpin > 0.f)
                    shard.mNode->setAttitude(
                        shard.mNode->getAttitude() * osg::Quat(shard.mMotion.mSpin * dt, shard.mSpinAxis));
                shard.mNode->setScale(osg::Vec3f(scale, scale, scale));
            }
        }
        while (!mEffects.empty() && mEffects.front().mAge >= ShatterRules::Lifetime)
        {
            remove(mEffects.front());
            mEffects.pop_front();
        }
    }

    void ShatterEffects::remove(Effect& effect)
    {
        for (const auto& shard : effect.mShards)
            mParent->removeChild(shard.mNode);
        effect.mShards.clear();
    }

    void ShatterEffects::clear()
    {
        for (auto& effect : mEffects)
            remove(effect);
        mEffects.clear();
    }
}
