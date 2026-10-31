#include "render/InstancedQuadRenderer.h"

#include <glad/glad.h>

// 单位 quad(-0.5 ~ 0.5,2 分量顶点)
namespace {
constexpr float kQuadVertices[] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
     0.5f,  0.5f,

    -0.5f, -0.5f,
     0.5f,  0.5f,
    -0.5f,  0.5f
};
}

void InstancedQuadRenderer::InitInstancedQuad(size_t maxInstances, size_t instanceStride)
{
    instanceStride_ = instanceStride;

    vao_.Bind();
    quadVbo_.BufferData(kQuadVertices, sizeof(kQuadVertices), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    instanceVbo_.BufferData(nullptr, maxInstances * instanceStride, GL_DYNAMIC_DRAW);
    vao_.Unbind();
}

void InstancedQuadRenderer::DefineInstanceAttrib(const InstAttrib& attrib)
{
    vao_.Bind();
    glEnableVertexAttribArray(attrib.location);
    glVertexAttribPointer(attrib.location, attrib.components, GL_FLOAT, GL_FALSE,
                          (GLsizei)instanceStride_, (void*)attrib.offsetBytes);
    glVertexAttribDivisor(attrib.location, 1);
    vao_.Unbind();
}

void InstancedQuadRenderer::UploadInstances(const void* data, size_t bytes)
{
    instanceVbo_.BufferSubData(data, bytes, 0);
}

void InstancedQuadRenderer::DrawInstanced(size_t instanceCount)
{
    vao_.Bind();
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)instanceCount);
    vao_.Unbind();
}
