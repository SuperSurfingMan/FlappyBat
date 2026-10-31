#pragma once

#include <glad/glad.h>
#include <cstddef>

// VBO 的 RAII 封装：构造即生成，析构自动释放，禁止拷贝、允许移动。
class VertexBuffer
{
public:
    VertexBuffer();
    ~VertexBuffer();

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VertexBuffer(VertexBuffer&& other) noexcept;
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    // 上传数据（内部会先 Bind 到 GL_ARRAY_BUFFER，调用后保持绑定，
    // 以便紧接着配置 glVertexAttribPointer）。
    void BufferData(const void* data, size_t size, GLenum usage) const;

    // 更新数据子区间（内部会先 Bind 到 GL_ARRAY_BUFFER，调用后保持绑定）。
    void BufferSubData(const void* data, size_t size, GLintptr offset) const;

    GLuint GetID() const { return id; }

private:
    GLuint id = 0;
};
