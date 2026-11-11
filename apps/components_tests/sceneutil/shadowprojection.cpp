#include <components/sceneutil/shadowprojection.hpp>

#include <gtest/gtest.h>

namespace
{
    const osg::Matrixd validProjection
        = osg::Matrixd::lookAt(osg::Vec3d(8, -12, 17), osg::Vec3d(0, 0, 0), osg::Vec3d(0, 0, 1))
        * osg::Matrixd::ortho(-20, 20, -20, 20, 1, 100);
    const osg::Matrixd textureProjection
        = validProjection * osg::Matrixd::scale(1.7, .8, 1) * osg::Matrixd::translate(.2, -.1, 0);
    const osg::Vec4d point(2, 3, -1, 1);

    osg::Matrixd matrix(osg::StateSet& stateset, const std::string& name)
    {
        auto* uniform = stateset.getUniform(name);
        EXPECT_NE(uniform, nullptr) << name;
        osg::Matrixf value;
        if (uniform)
            EXPECT_TRUE(uniform->get(value));
        return osg::Matrixd(value);
    }

    void expectPoint(const osg::Vec4d& actual, const osg::Vec4d& expected)
    {
        for (int i = 0; i < 4; ++i)
            EXPECT_NEAR(actual[i], expected[i], 1e-5) << i;
    }

    osg::Vec4d texturePoint()
    {
        const auto clip = point * textureProjection;
        return osg::Vec4d((clip.x() + clip.w()) * .5, (clip.y() + clip.w()) * .5, (clip.z() + clip.w()) * .5, clip.w());
    }

    TEST(ShadowProjectionTest, reprojects_both_coordinates_when_the_camera_moves_or_turns)
    {
        SceneUtil::ShadowProjection projection;
        projection.setMatrices(validProjection, textureProjection, true);
        const std::array<osg::Matrixd, 3> views{ osg::Matrixd::identity(), osg::Matrixd::translate(-3, 7, -2),
            osg::Matrixd::rotate(.7, osg::Vec3d(0, 0, 1)) * osg::Matrixd::translate(-9, 4, -3) };
        for (unsigned int i = 0; i < views.size(); ++i)
        {
            projection.updateUniforms(i, 0, osg::Matrixd::inverse(views[i]));
            osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
            projection.apply(*state, i);
            const auto eyePoint = point * views[i];
            expectPoint(eyePoint * matrix(*state, "validRegionMatrix0"), point * validProjection);
            expectPoint(eyePoint * matrix(*state, "shadowSpaceMatrix0"), texturePoint());
        }
    }

    TEST(ShadowProjectionTest, updating_one_traversal_buffer_preserves_the_other)
    {
        SceneUtil::ShadowProjection projection;
        projection.setMatrices(validProjection, textureProjection, true);
        projection.updateUniforms(0, 0, osg::Matrixd::identity());
        osg::ref_ptr<osg::StateSet> previous = new osg::StateSet;
        projection.apply(*previous, 0);
        const auto before = matrix(*previous, "validRegionMatrix0");
        projection.updateUniforms(1, 0, osg::Matrixd::translate(10, 0, 0));
        osg::ref_ptr<osg::StateSet> current = new osg::StateSet;
        projection.apply(*current, 1);
        EXPECT_NE(previous->getUniform("validRegionMatrix0"), current->getUniform("validRegionMatrix0"));
        expectPoint(point * matrix(*previous, "validRegionMatrix0"), point * before);
    }

    TEST(ShadowProjectionTest, views_share_projection_values_without_sharing_mutable_uniforms)
    {
        SceneUtil::ShadowProjection source, secondView;
        source.setMatrices(validProjection, textureProjection, true);
        source.updateUniforms(0, 0, osg::Matrixd::identity());
        osg::ref_ptr<osg::StateSet> first = new osg::StateSet;
        source.apply(*first, 0);
        secondView.copyMatrices(source);
        secondView.updateUniforms(0, 0, osg::Matrixd::translate(-4, 2, 0));
        osg::ref_ptr<osg::StateSet> second = new osg::StateSet;
        secondView.apply(*second, 0);
        EXPECT_NE(first->getUniform("shadowSpaceMatrix0"), second->getUniform("shadowSpaceMatrix0"));
        expectPoint(point * matrix(*first, "shadowSpaceMatrix0"), texturePoint());
    }

    TEST(ShadowProjectionTest, changing_map_index_keeps_previous_render_state_alive)
    {
        SceneUtil::ShadowProjection projection;
        projection.setMatrices(validProjection, textureProjection, true);
        projection.updateUniforms(0, 0, osg::Matrixd::identity());
        osg::ref_ptr<osg::StateSet> old = new osg::StateSet;
        projection.apply(*old, 0);
        projection.updateUniforms(0, 3, osg::Matrixd::translate(-4, 0, 0));
        osg::ref_ptr<osg::StateSet> current = new osg::StateSet;
        projection.apply(*current, 0);
        EXPECT_NE(current->getUniform("shadowSpaceMatrix3"), nullptr);
        EXPECT_EQ(current->getUniform("shadowSpaceMatrix0"), nullptr);
        expectPoint(point * matrix(*old, "shadowSpaceMatrix0"), texturePoint());
    }

    TEST(ShadowProjectionTest, orthographic_shadows_skip_unused_region_uniforms)
    {
        SceneUtil::ShadowProjection projection;
        projection.setMatrices(validProjection, textureProjection, false);
        const auto view = osg::Matrixd::translate(5, -3, 1);
        projection.updateUniforms(1, 0, osg::Matrixd::inverse(view));
        osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        projection.apply(*state, 1);
        EXPECT_EQ(state->getUniform("validRegionMatrix0"), nullptr);
        expectPoint(point * view * matrix(*state, "shadowSpaceMatrix0"), texturePoint());
    }

    TEST(ShadowProjectionTest, steady_state_updates_reuse_the_same_uniform_objects)
    {
        SceneUtil::ShadowProjection projection;
        projection.setMatrices(validProjection, textureProjection, true);
        projection.updateUniforms(0, 0, osg::Matrixd::identity());
        osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        projection.apply(*state, 0);
        const auto* shadow = state->getUniform("shadowSpaceMatrix0");
        const auto* region = state->getUniform("validRegionMatrix0");
        for (int i = 0; i < 100; ++i)
        {
            projection.updateUniforms(0, 0, osg::Matrixd::translate(i, 0, 0));
            projection.apply(*state, 0);
            EXPECT_EQ(state->getUniform("shadowSpaceMatrix0"), shadow);
            EXPECT_EQ(state->getUniform("validRegionMatrix0"), region);
        }
    }
}
