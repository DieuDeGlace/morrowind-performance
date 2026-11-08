#include "apps/openmw/mwphysics/ragdollmorph.hpp"
#include <gtest/gtest.h>
#include <osg/Geode>

namespace
{
    struct Body
    {
        osg::ref_ptr<osg::MatrixTransform> mFrame = new osg::MatrixTransform;
        osg::ref_ptr<SceneUtil::MorphGeometry> mMorph = new SceneUtil::MorphGeometry;
        Nif::NiMorphData mData;
        Nif::NiGeomMorpherController mController;
        Body()
        {
            osg::ref_ptr<osg::Vec3Array> p = new osg::Vec3Array, delta = new osg::Vec3Array;
            for (int y = 0; y < 25; ++y)
                for (int i = 0; i < 8; ++i)
                {
                    const float angle = i * 6.2831853f / 8, along = y * 3.f - 36.f;
                    p->emplace_back(3.f * std::cos(angle), along, 3.f * std::sin(angle));
                    delta->emplace_back(0, -along * .15f, 8.f * std::sin(along * .06f));
                }
            osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
            source->setVertexArray(p);
            source->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, p->size()));
            mMorph->setName("Synthetic body");
            mMorph->setSourceGeometry(source);
            mMorph->addMorphTarget(p);
            mMorph->addMorphTarget(delta, .75f);
            mData.mMorphs.resize(2);
            for (auto& morph : mData.mMorphs)
            {
                auto keys = std::make_shared<Nif::FloatKeyMap>();
                keys->mInterpolationType = Nif::InterpolationType_Linear;
                keys->mKeys.emplace_back(0.f, Nif::KeyT<float>{ 0, 0, 0 });
                keys->mKeys.emplace_back(.5f, Nif::KeyT<float>{ 1, 0, 0 });
                keys->mKeys.emplace_back(1.f, Nif::KeyT<float>{ 0, 0, 0 });
                morph.mKeyFrames = keys;
            }
            mController.mData = &mData;
            mMorph->setUpdateCallback(new NifOsg::GeomMorpherController(&mController));
            osg::ref_ptr<osg::Geode> geode = new osg::Geode;
            geode->addDrawable(mMorph);
            mFrame->addChild(geode);
        }
    };
    class FindRig : public osg::NodeVisitor
    {
    public:
        FindRig()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        {
        }
        void apply(osg::Drawable& drawable) override
        {
            if (auto* rig = dynamic_cast<SceneUtil::RigGeometry*>(&drawable))
                mRig = rig;
        }
        osg::ref_ptr<SceneUtil::RigGeometry> mRig;
    };
    TEST(RagdollMorph, KeepsExactDeathPoseAndRestoresOriginalMorphOnResurrection)
    {
        Body body;
        const auto mask = body.mMorph->getNodeMask();
        const auto callback = body.mMorph->getUpdateCallback();
        const auto frozen = MWPhysics::MorphRagdollDetail::pose(*body.mMorph);
        {
            MWPhysics::RagdollMorphRig rig(*body.mFrame);
            ASSERT_TRUE(rig.converted());
            EXPECT_EQ(body.mMorph->getNodeMask(), 0);
            FindRig find;
            body.mFrame->accept(find);
            ASSERT_TRUE(find.mRig);
            auto* vertices = dynamic_cast<const osg::Vec3Array*>(find.mRig->getSourceGeometry()->getVertexArray());
            ASSERT_TRUE(vertices);
            ASSERT_EQ(vertices->size(), frozen.size());
            for (std::size_t i = 0; i < frozen.size(); ++i)
                EXPECT_LT(((*vertices)[i] - frozen[i]).length(), .00001f);
            const auto automatic = MWPhysics::makeAutoRagdoll(*body.mFrame, osg::Matrixf::identity());
            EXPECT_GE(automatic.mParts.size(), 5);
            EXPECT_LE(automatic.mParts.size(), 8);
            rig.restore();
            EXPECT_EQ(body.mMorph->getNodeMask(), mask);
            FindRig restored;
            body.mFrame->accept(restored);
            EXPECT_FALSE(restored.mRig);
        }
        EXPECT_EQ(body.mMorph->getUpdateCallback(), callback);
        EXPECT_FLOAT_EQ(body.mMorph->getMorphTarget(1).getWeight(), .75f);
        EXPECT_EQ(body.mMorph->getNodeMask(), mask);
    }
    TEST(RagdollMorph, InvalidMotionKeepsLivingRepresentationAndOriginalVisibility)
    {
        Body body;
        body.mMorph->setUpdateCallback(nullptr);
        MWPhysics::RagdollMorphRig rig(*body.mFrame);
        EXPECT_FALSE(rig.converted());
        EXPECT_NE(body.mMorph->getNodeMask(), 0);
    }
    TEST(RagdollMorph, SeparateActorsNeverShareGeneratedBonesOrVisibility)
    {
        Body a, b;
        for (std::size_t i = 0; i < a.mMorph->getMorphTargetList().size(); ++i)
            b.mMorph->getMorphTarget(i).setOffsets(a.mMorph->getMorphTarget(i).getOffsets());
        MWPhysics::RagdollMorphRig first(*a.mFrame);
        ASSERT_TRUE(first.converted());
        EXPECT_NE(b.mMorph->getNodeMask(), 0);
        MWPhysics::RagdollMorphRig second(*b.mFrame);
        ASSERT_TRUE(second.converted());
        const auto x = MWPhysics::makeAutoRagdoll(*a.mFrame, osg::Matrixf::identity());
        const auto y = MWPhysics::makeAutoRagdoll(*b.mFrame, osg::Matrixf::identity());
        ASSERT_FALSE(x.mParts.empty());
        ASSERT_FALSE(y.mParts.empty());
        EXPECT_NE(x.mParts[0].mNode, y.mParts[0].mNode);
        first.restore();
        EXPECT_NE(a.mMorph->getNodeMask(), 0);
        EXPECT_EQ(b.mMorph->getNodeMask(), 0);
    }
    TEST(RagdollMorph, HybridBodyRigStaysBelowItsOriginalOwnerBone)
    {
        Body body;
        osg::ref_ptr<osg::MatrixTransform> owner = new osg::MatrixTransform;
        owner->setName("Original torso");
        owner->setMatrix(osg::Matrixf::translate(20, 30, 40));
        osg::ref_ptr<osg::Node> child = body.mFrame->getChild(0);
        body.mFrame->removeChild(child);
        owner->addChild(child);
        body.mFrame->addChild(owner);
        osg::ref_ptr<osg::Geometry> rigid = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(-4, -5, -4));
        vertices->push_back(osg::Vec3f(4, 5, 4));
        rigid->setVertexArray(vertices);
        rigid->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, 2));
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(rigid);
        owner->addChild(geode);
        MWPhysics::RagdollMorphRig morph(*body.mFrame);
        ASSERT_TRUE(morph.converted());
        const auto rig = MWPhysics::makeAutoRagdoll(*body.mFrame, osg::Matrixf::identity());
        ASSERT_GT(rig.mParts.size(), 5);
        EXPECT_EQ(rig.mParts.front().mNode, owner);
        EXPECT_NE(rig.mParts.front().mNode, body.mFrame);
        morph.restore();
        EXPECT_EQ(owner->getNumChildren(), 2);
        EXPECT_NE(body.mMorph->getNodeMask(), 0);
    }

}
