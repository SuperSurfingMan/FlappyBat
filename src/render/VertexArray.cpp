#include "render/VertexArray.h"

VertexArray::VertexArray()
{
    glGenVertexArrays(1, &id);
}

VertexArray::~VertexArray()
{
    if (id != 0)
        glDeleteVertexArrays(1, &id);
}

VertexArray::VertexArray(VertexArray&& other) noexcept
{
    id = other.id;
    other.id = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept
{
    if (this != &other)
    {
        if (id != 0)
            glDeleteVertexArrays(1, &id);
        id = other.id;
        other.id = 0;
    }
    return *this;
}

void VertexArray::Bind() const
{
    glBindVertexArray(id);
}

void VertexArray::Unbind() const
{
    glBindVertexArray(0);
}
