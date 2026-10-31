#include "render/ShaderLibrary.h"

#include "render/Shader.h"

ShaderLibrary& ShaderLibrary::Instance()
{
    static ShaderLibrary instance;
    return instance;
}

std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& name,
                                            const std::string& vertexPath,
                                            const std::string& fragmentPath)
{
    auto it = shaders_.find(name);
    if (it != shaders_.end())
        return it->second;

    auto shader = std::make_shared<Shader>(vertexPath.c_str(), fragmentPath.c_str());
    shaders_[name] = shader;
    return shader;
}

std::shared_ptr<Shader> ShaderLibrary::Get(const std::string& name) const
{
    auto it = shaders_.find(name);
    if (it == shaders_.end())
        return nullptr;
    return it->second;
}

bool ShaderLibrary::IsLoaded(const std::string& name) const
{
    return shaders_.find(name) != shaders_.end();
}

void ShaderLibrary::Clear()
{
    shaders_.clear();
}
