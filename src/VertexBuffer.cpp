#include "VertexBuffer.h"

VertexBuffer::VertexBuffer()
{
    glGenBuffers(1, &id);
}

VertexBuffer::~VertexBuffer()
{
    if (id != 0)
        glDeleteBuffers(1, &id);
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
{
    id = other.id;
    other.id = 0;
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
{
    if (this != &other)
    {
        if (id != 0)
            glDeleteBuffers(1, &id);
        id = other.id;
        other.id = 0;
    }
    return *this;
}

void VertexBuffer::Bind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, id);
}

void VertexBuffer::Unbind() const
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::BufferData(const void* data, size_t size, GLenum usage) const
{
    Bind();
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, usage);
}

void VertexBuffer::BufferSubData(const void* data, size_t size, GLintptr offset) const
{
    Bind();
    glBufferSubData(GL_ARRAY_BUFFER, offset, static_cast<GLsizeiptr>(size), data);
}
