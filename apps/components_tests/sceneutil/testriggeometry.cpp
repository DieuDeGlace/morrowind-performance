#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/skeleton.hpp>

#include <gtest/gtest.h>
#include <osg/MatrixTransform>
#include <osgUtil/UpdateVisitor>

namespace
{
    osg::BoundingBox updateRig(
        const std::string& skeletonName, const std::string& meshName, const std::string* parentName = nullptr)
    {
        osg::ref_ptr<SceneUtil::Skeleton> skeleton = new SceneUtil::Skeleton;
        skeleton->setName(skeletonName);
        osg::ref_ptr<osg::MatrixTransform> bone = new osg::MatrixTransform;
        bone->setName("bone");
        bone->setMatrix(osg::Matrix::translate(3, 4, 5));
        skeleton->addChild(bone);

        osg::ref_ptr<osg::Geometry> source = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3(0, 0, 0));
        vertices->push_back(osg::Vec3(1, 0, 0));
        vertices->push_back(osg::Vec3(0, 1, 0));
        source->setVertexArray(vertices);
        source->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));

        osg::ref_ptr<SceneUtil::RigGeometry> rig = new SceneUtil::RigGeometry;
        rig->setName(meshName);
        rig->setSourceGeometry(source);
        rig->setBoneInfo({ { "bone", osg::BoundingSpheref(osg::Vec3f(0, 0, 0), 1), osg::Matrixf::identity() } });
        rig->setInfluences(std::vector<SceneUtil::RigGeometry::BoneWeights>(3, { { 0, 1.f } }));
        rig->setTransform(osg::Matrixf::identity());

        if (parentName)
        {
            osg::ref_ptr<osg::MatrixTransform> parent = new osg::MatrixTransform;
            parent->setName(*parentName);
            parent->setMatrix(osg::Matrix::translate(100, 0, 0));
            parent->addChild(rig);
            skeleton->addChild(parent);
        }
        else
            skeleton->addChild(rig);

        osgUtil::UpdateVisitor visitor;
        visitor.setTraversalNumber(1);
        skeleton->accept(visitor);
        return rig->getBoundingBox();
    }

    void expectBounds(const osg::BoundingBox& bounds, float x)
    {
        ASSERT_TRUE(bounds.valid());
        EXPECT_NEAR(bounds.xMin(), x - 1, 1e-5);
        EXPECT_NEAR(bounds.xMax(), x + 1, 1e-5);
        EXPECT_NEAR(bounds.yMin(), 3, 1e-5);
        EXPECT_NEAR(bounds.yMax(), 5, 1e-5);
        EXPECT_NEAR(bounds.zMin(), 4, 1e-5);
        EXPECT_NEAR(bounds.zMax(), 6, 1e-5);
    }

    TEST(RigGeometryTest, unnamed_mesh_can_attach_directly_to_unnamed_skeleton)
    {
        expectBounds(updateRig("", ""), 3);
    }

    TEST(RigGeometryTest, matching_names_do_not_include_the_skeleton_in_the_cancelled_path)
    {
        expectBounds(updateRig("same", "same"), 3);
    }

    TEST(RigGeometryTest, named_trishape_transform_keeps_existing_attachment_behavior)
    {
        const std::string parent = "trishape";
        expectBounds(updateRig("", "trishape", &parent), 3);
    }

    TEST(RigGeometryTest, optimized_out_trishape_cancels_the_remaining_parent_transform)
    {
        const std::string parent = "parent";
        expectBounds(updateRig("", "trishape", &parent), -97);
    }
}
