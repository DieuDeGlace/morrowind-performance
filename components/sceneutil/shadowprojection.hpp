#ifndef OPENMW_COMPONENTS_SCENEUTIL_SHADOWPROJECTION_H
#define OPENMW_COMPONENTS_SCENEUTIL_SHADOWPROJECTION_H

#include <array>
#include <limits>
#include <string>

#include <osg/Matrix>
#include <osg/StateSet>
#include <osg/Uniform>

namespace SceneUtil
{
    // Projections belong to the rendered texture, while uniforms belong to a
    // view and traversal buffer. Keep those lifetimes separate during reuse.
    class ShadowProjection
    {
    public:
        ShadowProjection() = default;
        ShadowProjection(const ShadowProjection&) = delete;
        ShadowProjection& operator=(const ShadowProjection&) = delete;

        void setMatrices(const osg::Matrixd& validRegion, const osg::Matrixd& shadow, bool useValidRegion)
        {
            mWorldToValidRegion = validRegion;
            mWorldToShadow = shadow;
            mWorldToShadow.postMultTranslate(osg::Vec3d(1, 1, 1));
            mWorldToShadow.postMultScale(osg::Vec3d(.5, .5, .5));
            mUseValidRegion = useValidRegion;
        }

        void copyMatrices(const ShadowProjection& source)
        {
            mWorldToValidRegion = source.mWorldToValidRegion;
            mWorldToShadow = source.mWorldToShadow;
            mUseValidRegion = source.mUseValidRegion;
        }

        void updateUniforms(unsigned int traversalNumber, unsigned int mapIndex, const osg::Matrixd& inverseView)
        {
            if (mMapIndex != mapIndex)
            {
                mMapIndex = mapIndex;
                mUniforms = {};
            }
            auto& uniforms = mUniforms[traversalNumber % 2];
            if (!uniforms.mShadowSpace)
                uniforms.mShadowSpace
                    = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "shadowSpaceMatrix" + std::to_string(mapIndex));
            uniforms.mShadowSpace->set(inverseView * mWorldToShadow);
            if (mUseValidRegion)
            {
                if (!uniforms.mValidRegion)
                    uniforms.mValidRegion
                        = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "validRegionMatrix" + std::to_string(mapIndex));
                uniforms.mValidRegion->set(inverseView * mWorldToValidRegion);
            }
        }

        void apply(osg::StateSet& stateset, unsigned int traversalNumber) const
        {
            const auto& uniforms = mUniforms[traversalNumber % 2];
            if (uniforms.mShadowSpace)
                stateset.addUniform(uniforms.mShadowSpace);
            if (mUseValidRegion && uniforms.mValidRegion)
                stateset.addUniform(uniforms.mValidRegion);
        }

    private:
        struct Uniforms
        {
            osg::ref_ptr<osg::Uniform> mShadowSpace;
            osg::ref_ptr<osg::Uniform> mValidRegion;
        };

        osg::Matrixd mWorldToValidRegion;
        osg::Matrixd mWorldToShadow;
        bool mUseValidRegion = false;
        unsigned int mMapIndex = std::numeric_limits<unsigned int>::max();
        std::array<Uniforms, 2> mUniforms;
    };
}

#endif
