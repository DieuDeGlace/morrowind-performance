// Standalone cull-test adapters for shader loading and game lighting.
// The fixture supplies its own sunlight and culls real shadow cameras
// without a GL context. Throw if it reaches an excluded dependency.
#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/shader/shadermanager.hpp>

#include <stdexcept>

namespace Shader
{
    osg::ref_ptr<osg::Shader> ShaderManager::getShader(std::string, const DefineMap&, std::optional<osg::Shader::Type>)
    {
        throw std::logic_error("Unexpected shader loader in cull test");
    }

    osg::ref_ptr<osg::Program> ShaderManager::getProgram(
        osg::ref_ptr<osg::Shader>, osg::ref_ptr<osg::Shader>, const osg::Program*)
    {
        throw std::logic_error("Unexpected shader loader in cull test");
    }
}

namespace SceneUtil
{
    osg::GLExtensions& getGLExtensions()
    {
        throw std::logic_error("Unexpected GL dependency in cull test");
    }

    osg::ref_ptr<osg::Light> LightManager::getSunlight()
    {
        throw std::logic_error("Unexpected light manager in cull test");
    }
}
