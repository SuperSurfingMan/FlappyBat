#pragma once

#include <string>
#include <unordered_map>

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();

    void Use() const;

    void SetInt(const char* name, int value) const;
    void SetFloat(const char* name, float value) const;
    void SetMat4(const char* name, const float* value) const;
    void SetVec3(const char* name, float x, float y, float z) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;
    
private:
    unsigned int programID;
    mutable std::unordered_map<std::string, int> uniformLocationCache;

    int GetUniformLocation(const char* name) const;
    std::string LoadFile(const char* path);
    unsigned int CompileShader(unsigned int type, const char* source);
};
