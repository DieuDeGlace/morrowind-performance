#include <components/sceneutil/mwshadowtechnique.hpp>

#include <gtest/gtest.h>
#include <osg/Geode>
#include <osg/Geometry>
#include <osgShadow/ShadowedScene>
#include <osgUtil/RenderStage>
#include <osgUtil/SceneView>

namespace
{
    class TestShadows final : public SceneUtil::MWShadowTechnique
    {
    public:
        void init() override
        {
            // The test culls real shadow cameras without drawing. Supplying an
            // empty program avoids loading game shaders or creating a GL context.
            for (auto& program : _castingPrograms)
                program = new osg::Program;
            MWShadowTechnique::init();
            enableShadows();
        }

        mutable unsigned int mapUpdates = 0;

        bool selectActiveLights(osgUtil::CullVisitor*, ViewDependentData* data) const override
        {
            ++mapUpdates;
            osg::ref_ptr<LightData> sun = new LightData(data);
            sun->directionalLight = true;
            sun->lightDir = osg::Vec3d(-1, -2, -3);
            sun->lightDir.normalize();
            data->getLightDataList().clear();
            data->getLightDataList().push_back(sun);
            return true;
        }
    };

    osg::ref_ptr<osg::Geode> triangle(float distance)
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3(-1, -1, -distance));
        vertices->push_back(osg::Vec3(1, -1, -distance));
        vertices->push_back(osg::Vec3(0, 1, -distance));
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geometry);
        return geode;
    }

    class Scene
    {
    public:
        explicit Scene(unsigned int interval, unsigned int maps = 2)
            : mapCount(maps)
            , shadows(new TestShadows)
            , root(new osgShadow::ShadowedScene)
        {
            shadows->setShadowUpdateInterval(interval);
            root->setShadowTechnique(shadows);
            auto* settings = root->getShadowSettings();
            settings->setShaderHint(osgShadow::ShadowSettings::NO_SHADERS);
            settings->setNumShadowMapsPerLight(maps);
            settings->setTextureSize(osg::Vec2s(4096, 4096));
            settings->setMaximumShadowMapDistance(8192);
            settings->setMultipleShadowMapHint(osgShadow::ShadowSettings::CASCADED);
            settings->setShadowMapProjectionHint(osgShadow::ShadowSettings::PERSPECTIVE_SHADOW_MAP);
            settings->setComputeNearFarModeOverride(osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);
            root->addChild(triangle(10));
            root->addChild(triangle(1000));
            osg::ref_ptr<osgUtil::CullVisitor::Identifier> identifier = new osgUtil::CullVisitor::Identifier;
            for (auto& view : views)
            {
                view = new osgUtil::SceneView;
                view->setDefaults();
                view->setSceneData(root);
                view->setViewport(0, 0, 128, 128);
                // OpenMW uses an unreversed cull projection with fixed clipping,
                // including when the draw shaders use reverse-Z.
                view->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
                // osgViewer::Renderer assigns one identifier to the two cull
                // visitors of a view, and a different identifier to each eye.
                view->getCullVisitor()->setIdentifier(identifier);
            }
            views[1]->setCamera(views[0]->getCamera(), false);
            shadows->init();
        }

        void cull(unsigned int frame, double time, const osg::Matrixd& eye = osg::Matrixd::identity(), double fov = 60,
            unsigned int viewIndex = 0)
        {
            auto& view = views.at(viewIndex);
            osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
            stamp->setFrameNumber(frame);
            stamp->setReferenceTime(time);
            stamp->setSimulationTime(time);
            view->setFrameStamp(stamp);
            view->getCullVisitor()->setTraversalNumber(frame);
            view->setProjectionMatrixAsPerspective(fov, 1, .1, 100000);
            view->setViewMatrix(eye);
            view->cull();
            ASSERT_EQ(shadows->getViewDependentData(view->getCullVisitor())->numValidShadows(), mapCount);
        }

        unsigned int mapCount;
        osg::ref_ptr<TestShadows> shadows;
        osg::ref_ptr<osgShadow::ShadowedScene> root;
        std::array<osg::ref_ptr<osgUtil::SceneView>, 2> views;

        osg::ref_ptr<osg::Uniform> shadowUniform(unsigned int frame, unsigned int viewIndex = 0)
        {
            auto* data = shadows->getViewDependentData(views.at(viewIndex)->getCullVisitor());
            return data->getStateSet(frame)->getUniform("shadowSpaceMatrix0");
        }

        osg::Vec4d shadowPoint(unsigned int frame, const osg::Matrixd& eye, unsigned int viewIndex = 0)
        {
            auto uniform = shadowUniform(frame, viewIndex);
            EXPECT_NE(uniform, nullptr);
            osg::Matrixf matrix;
            if (uniform)
                EXPECT_TRUE(uniform->get(matrix));
            return osg::Vec4d(0, 0, -10, 1) * eye * osg::Matrixd(matrix);
        }
    };

    void expectPoint(const osg::Vec4d& actual, const osg::Vec4d& expected)
    {
        for (unsigned int i = 0; i < 4; ++i)
            EXPECT_NEAR(actual[i], expected[i], 1e-4);
    }

    TEST(ShadowUpdateTest, stationary_view_keeps_interval_savings)
    {
        for (unsigned int interval : { 2u, 3u, 4u })
        {
            SCOPED_TRACE(interval);
            Scene scene(interval);
            scene.cull(0, 1);
            for (unsigned int frame = 1; frame < interval; ++frame)
                scene.cull(frame, 1 + frame * .005);
            EXPECT_EQ(scene.shadows->mapUpdates, 1);
            scene.cull(interval, 1 + interval * .005);
            EXPECT_EQ(scene.shadows->mapUpdates, 2);
        }
    }

    TEST(ShadowUpdateTest, interval_two_keeps_savings_and_the_latest_map_while_walking_at_sixty_fps)
    {
        Scene scene(2);
        osg::Vec4d lastRenderedPoint;
        for (unsigned int frame = 0; frame < 12; ++frame)
        {
            const auto eye = osg::Matrixd::translate(-double(frame), 0, 0);
            scene.cull(frame, 1 + frame / 60.0, eye, 60, frame % 2);
            EXPECT_EQ(scene.shadows->mapUpdates, frame / 2 + 1);
            const auto point = scene.shadowPoint(frame, eye, frame % 2);
            if (frame % 2 == 0)
                lastRenderedPoint = point;
            else
                expectPoint(point, lastRenderedPoint);
        }
    }

    TEST(ShadowUpdateTest, turning_reprojects_the_cached_map)
    {
        Scene scene(4);
        scene.cull(0, 1);
        const auto first = scene.shadowPoint(0, osg::Matrixd::identity());
        const auto eye = osg::Matrixd::rotate(.01, osg::Vec3d(0, 1, 0));
        scene.cull(1, 1.005, eye);
        EXPECT_EQ(scene.shadows->mapUpdates, 1);
        expectPoint(scene.shadowPoint(1, eye), first);
    }

    TEST(ShadowUpdateTest, changing_projection_refreshes_the_shadow_maps)
    {
        Scene scene(4);
        scene.cull(0, 1);
        scene.cull(1, 1.005, osg::Matrixd::identity(), 65);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
    }

    TEST(ShadowUpdateTest, distinct_view_identifiers_keep_independent_histories)
    {
        Scene scene(4);
        scene.views[1]->getCullVisitor()->setIdentifier(new osgUtil::CullVisitor::Identifier);
        scene.cull(0, 1);
        const auto first = scene.shadowPoint(0, osg::Matrixd::identity());
        scene.cull(1, 1.005, osg::Matrixd::translate(-5, 0, 0), 60, 1);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
        scene.cull(2, 1.01);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
        expectPoint(scene.shadowPoint(2, osg::Matrixd::identity()), first);
    }

    TEST(ShadowUpdateTest, expired_stationary_cache_still_refreshes)
    {
        Scene scene(4);
        scene.cull(0, 1);
        scene.cull(1, 1.05);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
    }

    TEST(ShadowUpdateTest, interval_one_always_refreshes)
    {
        Scene scene(1);
        scene.cull(0, 1);
        scene.cull(1, 1.005, osg::Matrixd::identity(), 60, 1);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
    }

    TEST(ShadowUpdateTest, updating_the_other_render_buffer_preserves_in_flight_uniforms)
    {
        Scene scene(1);
        scene.cull(0, 1);
        auto previous = scene.shadowUniform(0);
        ASSERT_NE(previous, nullptr);
        osg::Matrixf before;
        ASSERT_TRUE(previous->get(before));
        scene.cull(1, 1.005, osg::Matrixd::translate(-5, 0, 0), 60, 1);
        EXPECT_NE(previous, scene.shadowUniform(1, 1));
        osg::Matrixf after;
        ASSERT_TRUE(previous->get(after));
        EXPECT_EQ(before, after);
    }
}

namespace
{
    constexpr unsigned int movingMask = 1u << 4;
    constexpr unsigned int sceneryMask = 1u << 11;

    struct ShadowVisits final : osg::NodeCallback
    {
        unsigned int scenery = 0;
        unsigned int moving = 0;

        void operator()(osg::Node* node, osg::NodeVisitor* visitor) override
        {
            auto* cv = visitor->asCullVisitor();
            auto* camera = cv->getCurrentRenderBin()->getStage()->getCamera();
            if (camera && camera->getName() == "StaticShadowCamera")
                ++scenery;
            else if (camera && camera->getName() == "ShadowCamera")
                ++moving;
            traverse(node, visitor);
        }
    };

    void addCaster(Scene& scene, unsigned int mask, float distance, ShadowVisits* visits)
    {
        auto caster = triangle(distance);
        caster->setNodeMask(mask);
        caster->setCullingActive(false);
        caster->setCullCallback(visits);
        scene.root->addChild(caster);
    }

    TEST(ShadowUpdateTest, moving_casters_are_culled_every_frame_while_scenery_is_reused)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        scene.root->removeChildren(0, scene.root->getNumChildren());
        osg::ref_ptr<ShadowVisits> scenery = new ShadowVisits;
        osg::ref_ptr<ShadowVisits> moving = new ShadowVisits;
        addCaster(scene, sceneryMask, 1000, scenery);
        addCaster(scene, movingMask, 10, moving);
        for (unsigned int frame = 0; frame < 48; ++frame)
            scene.cull(frame, 1 + frame / 60., osg::Matrixd::translate(-double(frame), 0, 0), 60, frame % 2);
        EXPECT_EQ(scene.shadows->mapUpdates, 24);
        EXPECT_EQ(scenery->scenery, 24);
        EXPECT_EQ(scenery->moving, 0);
        EXPECT_EQ(moving->scenery, 0);
        EXPECT_EQ(moving->moving, 48);
    }

    TEST(ShadowUpdateTest, interval_one_uses_the_existing_complete_map_path)
    {
        Scene scene(1, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        for (unsigned int frame = 0; frame < 4; ++frame)
            scene.cull(frame, 1 + frame / 60.);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        EXPECT_EQ(scene.shadows->mapUpdates, 4);
        EXPECT_FALSE(data->getShadowDataList().front()->_backgroundTexture);
    }

    TEST(ShadowUpdateTest, unsupported_depth_restore_keeps_moving_shadows_fresh)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask, false);
        for (unsigned int frame = 0; frame < 4; ++frame)
            scene.cull(frame, 1 + frame / 60.);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        EXPECT_EQ(scene.shadows->mapUpdates, 4);
        EXPECT_FALSE(data->getShadowDataList().front()->_backgroundTexture);
    }

    TEST(ShadowUpdateTest, cached_depth_resizes_when_only_one_texture_dimension_changes)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        scene.cull(0, 1);
        scene.cull(1, 1 + 1 / 60.);
        scene.root->getShadowSettings()->setTextureSize(osg::Vec2s(4096, 2048));
        scene.cull(2, 1 + 2 / 60.);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        const auto& shadow = data->getShadowDataList().front();
        ASSERT_TRUE(shadow->_backgroundTexture);
        EXPECT_EQ(shadow->_texture->getTextureHeight(), 2048);
        EXPECT_EQ(shadow->_backgroundTexture->getTextureHeight(), 2048);
        EXPECT_EQ(shadow->_camera->getViewport()->height(), 2048);
        EXPECT_EQ(shadow->_staticCamera->getViewport()->height(), 2048);
    }

    TEST(ShadowUpdateTest, disabling_reuse_releases_the_extra_cached_depth_map)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        scene.cull(0, 1);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        ASSERT_TRUE(data->getShadowDataList().front()->_backgroundTexture);
        scene.shadows->setShadowUpdateInterval(1);
        scene.cull(1, 1 + 1 / 60.);
        EXPECT_FALSE(data->getShadowDataList().front()->_backgroundTexture);
        EXPECT_FALSE(data->getShadowDataList().front()->_depthRestore[0]);
    }
}

namespace
{
    TEST(ShadowUpdateTest, moving_only_shadow_pass_does_not_allocate_a_static_depth_cache)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        scene.shadows->setWorldMask(sceneryMask);
        scene.root->getShadowSettings()->setCastsShadowTraversalMask(movingMask);
        for (unsigned int frame = 0; frame < 4; ++frame)
            scene.cull(frame, 1 + frame / 60.);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        EXPECT_EQ(scene.shadows->mapUpdates, 4);
        EXPECT_FALSE(data->getShadowDataList().front()->_backgroundTexture);
    }

    TEST(ShadowUpdateTest, changing_caster_mask_invalidates_the_cached_depth_pass)
    {
        Scene scene(2, 1);
        scene.shadows->setDynamicCasterMask(movingMask);
        scene.cull(0, 1);
        auto* data = scene.shadows->getViewDependentData(scene.views[0]->getCullVisitor());
        ASSERT_TRUE(data->getShadowDataList().front()->_backgroundTexture);
        scene.root->getShadowSettings()->setCastsShadowTraversalMask(sceneryMask);
        scene.cull(1, 1 + 1 / 60.);
        EXPECT_EQ(scene.shadows->mapUpdates, 2);
        EXPECT_FALSE(data->getShadowDataList().front()->_backgroundTexture);
    }
}
