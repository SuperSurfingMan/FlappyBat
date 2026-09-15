#pragma once

#include <glad/glad.h>

// VAO 的 RAII 封装：构造即生成，析构自动释放，禁止拷贝、允许移动。
class VertexArray
{
public:
    VertexArray();
    ~VertexArray();

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;

    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    GLuint GetID() const { return id; }

private:
    GLuint id = 0;
};
