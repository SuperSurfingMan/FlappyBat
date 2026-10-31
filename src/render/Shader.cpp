#include "render/Shader.h"

#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    std::string vertexCode = LoadFile(vertexPath);
    std::string fragmentCode = LoadFile(fragmentPath);

    const char* vsrc = vertexCode.c_str();
    const char* fsrc = fragmentCode.c_str();

    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vsrc);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fsrc);

    programID = glCreateProgram();
    glAttachShader(programID, vs);
    glAttachShader(programID, fs);
    glLinkProgram(programID);

    int success;
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(programID, 512, nullptr, info);
        std::cerr << "[Shader] Program link error:\n" << info << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::~Shader()
{
    glDeleteProgram(programID);
}

void Shader::Use() const
{
    glUseProgram(programID);
}

int Shader::GetUniformLocation(const char* name) const
{
    if (uniformLocationCache.find(name) != uniformLocationCache.end()) {
        return uniformLocationCache[name];
    }
    int location = glGetUniformLocation(programID, name);
    uniformLocationCache[name] = location;
    return location;
}

void Shader::SetInt(const char* name, int value) const
{
    glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetFloat(const char* name, float value) const
{
    glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetMat4(const char* name, const float* value) const
{
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, value);
}

void Shader::SetVec4(const char* name,
                     float x, float y,
                     float z, float w) const
{
    glUniform4f(GetUniformLocation(name), x, y, z, w);
}

void Shader::SetVec3(const char* name,
                     float x, float y,
                     float z) const
{
    glUniform3f(GetUniformLocation(name), x, y, z);
}

std::string Shader::LoadFile(const char* path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Shader] Failed to open file: " << path << std::endl;
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

unsigned int Shader::CompileShader(unsigned int type, const char* source)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "[Shader] Compile error:\n" << info << std::endl;
    }

    return shader;
}
