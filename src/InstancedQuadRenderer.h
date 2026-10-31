#pragma once

#include "IRenderer.h"
#include "VertexArray.h"
#include "VertexBuffer.h"

#include <cstddef>

// =============================================================================
// 实例化四边形渲染器的公共基类(方案一):收纳 Rain/Wind 共用的 GL 样板——
// 单位 quad VBO(location 0)+ 实例数据 VBO + 逐属性布局声明 + 实例化绘制。
// 子类只负责:取 shader、声明实例属性、填实例数据、设 uniform。
// =============================================================================
class InstancedQuadRenderer : public IRenderer
{
public:
    // 一条实例属性描述:location(1 起)/分量数/在实例结构体内的字节偏移
    struct InstAttrib {
        int location;
        int components;
        size_t offsetBytes;
    };

protected:
    // 初始化单位 quad(location 0)与实例缓冲;instanceStride 为单个实例字节数
    void InitInstancedQuad(size_t maxInstances, size_t instanceStride);

    // 声明一条实例属性(location 1 起,divisor=1);须在 InitInstancedQuad 之后调用
    void DefineInstanceAttrib(const InstAttrib& attrib);

    // 上传实例数据并实例化绘制(先 Upload 后 Draw)
    void UploadInstances(const void* data, size_t bytes);
    void DrawInstanced(size_t instanceCount);

    VertexArray vao_;
    VertexBuffer quadVbo_;
    VertexBuffer instanceVbo_;

private:
    size_t instanceStride_ = 0;
};
