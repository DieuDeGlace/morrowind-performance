#include "apps/openmw/mwphysics/ragdollcontrollers.hpp"

#include <components/nifosg/controller.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/nodecallback.hpp>

#include <gtest/gtest.h>
#include <osgUtil/UpdateVisitor>

namespace
{
    class CountUpdates : public SceneUtil::NodeCallback<CountUpdates>
    {
    public:
        int mCount = 0;
        void operator()(osg::Node* node, osg::NodeVisitor* visitor)
        {
            ++mCount;
            traverse(node, visitor);
        }
    };

    TEST(RagdollControllers, EmbeddedNifControllerCannotResetPhysicsRotation)
    {
        osg::ref_ptr<NifOsg::MatrixTransform> bone = new NifOsg::MatrixTransform(Nif::NiTransform::getIdentity());
        bone->addUpdateCallback(new NifOsg::KeyframeController);
        osgUtil::UpdateVisitor update;
        const osg::Matrixf physicsPose = osg::Matrixf::rotate(.7f, osg::Vec3f(1, 0, 0));

        // Reproduce the failure: even a controller with no input restores its
        // cached NIF rotation over a matrix supplied by the ragdoll.
        bone->setMatrix(physicsPose);
        bone->accept(update);
        EXPECT_NE(bone->getMatrix(), physicsPose);

        MWPhysics::RagdollControllers controllers;
        controllers.suspend(*bone);
        bone->setMatrix(physicsPose);
        bone->accept(update);
        EXPECT_EQ(bone->getMatrix(), physicsPose);

        controllers.restore();
        bone->accept(update);
        EXPECT_NE(bone->getMatrix(), physicsPose);
    }

    TEST(RagdollControllers, FreezesMorphAnimationAndRestoresItForResurrection)
    {
        osg::ref_ptr<osg::Node> node = new osg::Node;
        osg::ref_ptr<NifOsg::GeomMorpherController> morph = new NifOsg::GeomMorpherController;
        osg::ref_ptr<CountUpdates> effect = new CountUpdates;
        node->addUpdateCallback(morph);
        node->addUpdateCallback(effect);
        MWPhysics::RagdollControllers controllers;
        controllers.suspend(*node);
        EXPECT_EQ(node->getUpdateCallback(), effect.get());
        controllers.restore();
        EXPECT_EQ(node->getUpdateCallback(), morph.get());
        EXPECT_EQ(morph->getNestedCallback(), effect.get());
    }

    TEST(RagdollControllers, PreservesEffectsAndRestoresOriginalCallbackOrder)
    {
        osg::ref_ptr<osg::Node> node = new osg::Node;
        osg::ref_ptr<CountUpdates> effect = new CountUpdates;
        osg::ref_ptr<osgAnimation::UpdateMatrixTransform> first = new osgAnimation::UpdateMatrixTransform;
        osg::ref_ptr<osgAnimation::UpdateMatrixTransform> second = new osgAnimation::UpdateMatrixTransform;
        node->addUpdateCallback(first);
        node->addUpdateCallback(second);
        node->addUpdateCallback(effect);

        {
            MWPhysics::RagdollControllers controllers;
            controllers.suspend(*node);
            controllers.suspend(*node); // Repeated suspension must not duplicate callbacks.
            ASSERT_EQ(node->getUpdateCallback(), effect.get());
            osgUtil::UpdateVisitor update;
            node->accept(update);
            EXPECT_EQ(effect->mCount, 1);
        }

        EXPECT_EQ(node->getUpdateCallback(), first.get());
        EXPECT_EQ(first->getNestedCallback(), second.get());
        EXPECT_EQ(second->getNestedCallback(), effect.get());
        EXPECT_EQ(effect->getNestedCallback(), nullptr);
    }

    TEST(RagdollControllers, RestoresControllerBetweenUnrelatedCallbacks)
    {
        osg::ref_ptr<osg::Node> node = new osg::Node;
        osg::ref_ptr<CountUpdates> before = new CountUpdates;
        osg::ref_ptr<CountUpdates> after = new CountUpdates;
        osg::ref_ptr<osgAnimation::UpdateMatrixTransform> controller = new osgAnimation::UpdateMatrixTransform;
        node->addUpdateCallback(before);
        node->addUpdateCallback(controller);
        node->addUpdateCallback(after);
        MWPhysics::RagdollControllers controllers;
        controllers.suspend(*node);
        EXPECT_EQ(before->getNestedCallback(), after.get());
        controllers.restore();
        controllers.restore();
        EXPECT_EQ(node->getUpdateCallback(), before.get());
        EXPECT_EQ(before->getNestedCallback(), controller.get());
        EXPECT_EQ(controller->getNestedCallback(), after.get());
    }
}