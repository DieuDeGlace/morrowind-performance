#include "apps/openmw/mwphysics/ragdollautorig.hpp"

#include <gtest/gtest.h>

#include <osg/Geode>
#include <osgParticle/ParticleSystem>

namespace
{
    osg::ref_ptr<osg::MatrixTransform> node(osg::Group& parent, const std::string& name, const osg::Vec3f& position)
    {
        osg::ref_ptr<osg::MatrixTransform> result = new osg::MatrixTransform;
        result->setName(name);
        result->setMatrix(osg::Matrixf::translate(position));
        parent.addChild(result);
        return result;
    }

    osg::ref_ptr<osg::Geometry> geometry()
    {
        osg::ref_ptr<osg::Geometry> result = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(-3, -12, -2));
        vertices->push_back(osg::Vec3f(3, 12, 2));
        result->setVertexArray(vertices);
        result->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 2));
        return result;
    }

    void attach(osg::Group& parent, osg::Drawable* drawable)
    {
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(drawable);
        parent.addChild(geode);
    }

    void skin(osg::Group& parent, const std::vector<std::string>& names)
    {
        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setSourceGeometry(geometry());
        std::vector<SceneUtil::RigGeometry::BoneInfo> bones;
        for (const auto& name : names)
            bones.push_back({ name, osg::BoundingSpheref(osg::Vec3f(), 3.f), osg::Matrixf::identity() });
        rig->setBoneInfo(std::move(bones));
        attach(parent, rig);
    }

    TEST(RagdollAutoRig, DiscoversUnnamedCreatureAnatomyAndPreservesItsBranching)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        osg::ref_ptr<SceneUtil::Skeleton> skeleton = new SceneUtil::Skeleton;
        frame->addChild(skeleton);
        auto motion = node(*skeleton, "transport", osg::Vec3f(0, 0, 100));
        auto torso = node(*motion, "carapace", osg::Vec3f());
        auto left = node(*torso, "appendage.a", osg::Vec3f(20, 0, 0));
        auto tip = node(*left, "appendage.a.end", osg::Vec3f(15, 0, 0));
        auto right = node(*torso, "appendage.b", osg::Vec3f(-20, 0, 0));
        // Skin names are case-insensitive, just like the renderer's lookup.
        skin(*skeleton, { "CARAPACE", "appendage.a", "appendage.a.end", "appendage.b" });

        auto rig = MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::translate(10, 30, 0));
        ASSERT_EQ(rig.mParts.size(), 4);
        EXPECT_EQ(rig.mParts[0].mNode.get(), torso.get());
        EXPECT_EQ(rig.mParts[1].mParent, 0);
        EXPECT_EQ(rig.mParts[2].mParent, 1);
        EXPECT_EQ(rig.mParts[3].mParent, 0);
        EXPECT_EQ(rig.mParts[1].mStart, osg::Vec3f(30, 30, 100));
        for (const auto& part : rig.mParts)
            EXPECT_NE(part.mNode.get(), motion.get());
    }

    TEST(RagdollAutoRig, VertexMorphedActorGetsWholeModelPhysicsWithoutInventingJoints)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        auto root = node(*frame, "root", osg::Vec3f());
        auto body = node(*root, "body", osg::Vec3f());
        auto shadow = node(*root, "shadow", osg::Vec3f());
        osg::ref_ptr<SceneUtil::MorphGeometry> morph = new SceneUtil::MorphGeometry;
        auto source = geometry();
        morph->setSourceGeometry(source);
        auto* vertices = static_cast<osg::Vec3Array*>(source->getVertexArray());
        morph->addMorphTarget(vertices);
        osg::ref_ptr<osg::Vec3Array> offsets = new osg::Vec3Array(2);
        std::fill(offsets->begin(), offsets->end(), osg::Vec3f(0, 50, 0));
        morph->addMorphTarget(offsets, .5f);
        attach(*body, morph);
        attach(*shadow, source);
        // A moving spit emitter is not a limb.
        auto emitter = node(*root, "emitter", osg::Vec3f(0, 200, 0));
        attach(*emitter, new osgParticle::ParticleSystem);

        const auto rig = MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity());
        ASSERT_EQ(rig.mParts.size(), 1);
        EXPECT_EQ(rig.mParts[0].mNode.get(), frame.get());
        // Fit the actual weighted morph pose, not all possible animation frames.
        EXPECT_FLOAT_EQ(rig.mParts[0].mStart.y(), -12.f);
        EXPECT_FLOAT_EQ(rig.mParts[0].mEnd.y(), 37.f);
        EXPECT_TRUE(std::ranges::any_of(rig.mAnimatedNodes, [&](const auto& n) { return n == morph; }));
        EXPECT_FALSE(std::ranges::any_of(rig.mAnimatedNodes, [&](const auto& n) { return n == emitter; }));
    }

    TEST(RagdollAutoRig, WholeModelFitFollowsActorRotationAndScale)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        attach(*frame, geometry());
        const osg::Matrixf world = osg::Matrixf::scale(2, 2, 2) * osg::Matrixf::rotate(.7f, osg::Vec3f(0, 0, 1))
            * osg::Matrixf::translate(100, 200, 50);
        const auto rig = MWPhysics::makeAutoRagdoll(*frame, world);
        ASSERT_EQ(rig.mParts.size(), 1);
        EXPECT_NEAR(rig.mParts[0].mRadius, 6.f, .001f);
        EXPECT_NEAR((rig.mParts[0].mEnd - rig.mParts[0].mStart).length(), 48.f, .001f);
        EXPECT_LT((rig.mParts[0].mStart - osg::Vec3f(0, -12, 0) * world).length(), .001f);
    }

    TEST(RagdollAutoRig, RigidAnimatedPartsWorkWithoutSkinWeights)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        auto body = node(*frame, "body", osg::Vec3f(0, 0, 40));
        auto leg = node(*body, "leg", osg::Vec3f(15, 0, -15));
        auto foot = node(*leg, "foot", osg::Vec3f(0, 0, -15));
        for (auto part : { body, leg, foot })
            attach(*part, geometry());
        const auto rig = MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity());
        ASSERT_EQ(rig.mParts.size(), 3);
        EXPECT_EQ(rig.mParts[0].mNode.get(), body.get());
        EXPECT_EQ(rig.mParts[2].mParent, 1);
    }

    TEST(RagdollAutoRig, BodyBudgetRetainsLargeLimbsAndReconnectsSkippedAncestors)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        auto torso = node(*frame, "core", osg::Vec3f());
        attach(*torso, geometry());
        auto left = node(*torso, "left", osg::Vec3f(50, 0, 0));
        attach(*left, geometry());
        for (int i = 0; i < 60; ++i)
        {
            auto finger = node(*left, "finger" + std::to_string(i), osg::Vec3f(0, .2f * i, 1));
            attach(*finger, geometry());
        }
        auto right = node(*torso, "right", osg::Vec3f(-50, 0, 0));
        attach(*right, geometry());
        const auto rig = MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity(), 8);
        ASSERT_EQ(rig.mParts.size(), 8);
        EXPECT_TRUE(std::ranges::any_of(rig.mParts, [&](const auto& part) { return part.mNode == left; }));
        EXPECT_TRUE(std::ranges::any_of(rig.mParts, [&](const auto& part) { return part.mNode == right; }));
        for (std::size_t i = 1; i < rig.mParts.size(); ++i)
            EXPECT_LT(rig.mParts[i].mParent, i);
    }

    TEST(RagdollAutoRig, SeparateSkeletonsResolveTheirOwnRepeatedBoneNames)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        for (float x : { -30.f, 30.f })
        {
            osg::ref_ptr<SceneUtil::Skeleton> skeleton = new SceneUtil::Skeleton;
            frame->addChild(skeleton);
            auto root = node(*skeleton, "body", osg::Vec3f(x, 0, 30));
            node(*root, "tip", osg::Vec3f(0, 0, 20));
            skin(*skeleton, { "body", "tip" });
        }
        const auto rig = MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity());
        ASSERT_EQ(rig.mParts.size(), 5);
        EXPECT_EQ(rig.mParts[1].mParent, 0);
        EXPECT_EQ(rig.mParts[2].mParent, 1);
        EXPECT_EQ(rig.mParts[3].mParent, 0);
        EXPECT_EQ(rig.mParts[4].mParent, 3);
    }

    TEST(RagdollAutoRig, MissingGeometryAndZeroBudgetFailCleanly)
    {
        osg::ref_ptr<osg::MatrixTransform> frame = new osg::MatrixTransform;
        node(*frame, "empty", osg::Vec3f());
        EXPECT_TRUE(MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity()).mParts.empty());
        attach(*frame, geometry());
        EXPECT_TRUE(MWPhysics::makeAutoRagdoll(*frame, osg::Matrixf::identity(), 0).mParts.empty());
    }

    TEST(RagdollAutoRig, FrameLivesOutsideSkeletonAndRestoresTheOriginalGraph)
    {
        osg::ref_ptr<osg::Group> parent = new osg::Group;
        osg::ref_ptr<SceneUtil::Skeleton> model = new SceneUtil::Skeleton;
        parent->addChild(model);
        {
            MWPhysics::RagdollModelFrame frame(*model);
            ASSERT_NE(frame.get(), nullptr);
            EXPECT_EQ(parent->getChild(0), frame.get());
            EXPECT_EQ(model->getParent(0), frame.get());
        }
        ASSERT_EQ(parent->getNumChildren(), 1);
        EXPECT_EQ(parent->getChild(0), model.get());
        ASSERT_EQ(model->getNumParents(), 1);
        EXPECT_EQ(model->getParent(0), parent.get());
    }
}
