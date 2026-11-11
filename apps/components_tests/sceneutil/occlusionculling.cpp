#include <components/sceneutil/occlusionculling.hpp>

#include <gtest/gtest.h>

namespace
{
    const osg::BoundingBox behind(-0.5f, -0.5f, -21.f, 0.5f, 0.5f, -19.f);
    const osg::BoundingBox inFront(-0.5f, -0.5f, -6.f, 0.5f, 0.5f, -4.f);
    const std::vector<osg::Vec3f> wall{
        { -8.f, -8.f, -10.f }, { 8.f, -8.f, -10.f }, { 8.f, 8.f, -10.f }, { -8.f, 8.f, -10.f }
    };
    const std::vector<unsigned int> wallIndices{ 0, 1, 2, 0, 2, 3 };

    void beginFrame(SceneUtil::OcclusionCuller& culler)
    {
        culler.beginFrame(osg::Matrixd::identity(), osg::Matrixd::perspective(90., 2., 1., 1000.));
    }

    TEST(OcclusionCullerTest, hides_geometry_behind_a_wall_but_keeps_geometry_in_front)
    {
        SceneUtil::OcclusionCuller culler(512, 256);
        ASSERT_TRUE(culler.isActive());
        beginFrame(culler);
        ASSERT_TRUE(culler.testVisibleAABB(behind));
        culler.rasterizeOccluder(wall, wallIndices);
        EXPECT_FALSE(culler.testVisibleAABB(behind));
        EXPECT_TRUE(culler.testVisibleAABB(inFront));
    }

    TEST(OcclusionCullerTest, clears_occlusion_when_a_new_frame_starts)
    {
        SceneUtil::OcclusionCuller culler(512, 256);
        beginFrame(culler);
        culler.rasterizeOccluder(wall, wallIndices);
        ASSERT_FALSE(culler.testVisibleAABB(behind));
        beginFrame(culler);
        EXPECT_TRUE(culler.testVisibleAABB(behind));
    }

    TEST(OcclusionCullerTest, keeps_objects_visible_outside_an_active_frame)
    {
        SceneUtil::OcclusionCuller culler(512, 256);
        EXPECT_TRUE(culler.testVisibleAABB(behind));
        beginFrame(culler);
        culler.rasterizeOccluder(wall, wallIndices);
        culler.endFrame();
        EXPECT_TRUE(culler.testVisibleAABB(behind));
    }

    TEST(OcclusionCullerTest, invalid_mesh_indices_cannot_occlude_objects)
    {
        SceneUtil::OcclusionCuller culler(512, 256);
        beginFrame(culler);
        culler.rasterizeOccluder(wall, { 0, 1, 100 });
        EXPECT_TRUE(culler.testVisibleAABB(behind));
    }

    TEST(OcclusionCullerTest, keeps_camera_crossing_bounds_visible)
    {
        SceneUtil::OcclusionCuller culler(512, 256);
        beginFrame(culler);
        culler.rasterizeOccluder(wall, wallIndices);
        EXPECT_TRUE(culler.testVisibleAABB(osg::BoundingBox(-1.f, -1.f, -2.f, 1.f, 1.f, 1.f)));
    }
}
