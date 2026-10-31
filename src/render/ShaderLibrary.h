#pragma once

#include <string>
#include <unordered_map>
#include <memory>

class Shader;

// 统一管理 shader 的加载、缓存与生命周期。
// 所有 shader 一律通过这里按名字加载/获取，禁止在业务类里自行 new Shader。
class ShaderLibrary
{
public:
    static ShaderLibrary& Instance();

    // 按名字加载并缓存；若同名已存在，直接返回缓存（不会重复编译）。
    std::shared_ptr<Shader> Load(const std::string& name,
                                 const std::string& vertexPath,
                                 const std::string& fragmentPath);

    // 获取已加载的 shader；未加载时返回 nullptr。
    std::shared_ptr<Shader> Get(const std::string& name) const;

    bool IsLoaded(const std::string& name) const;

    // 释放全部 shader（程序退出时调用）。
    void Clear();

private:
    ShaderLibrary() = default;
    ~ShaderLibrary() = default;

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders_;
};
