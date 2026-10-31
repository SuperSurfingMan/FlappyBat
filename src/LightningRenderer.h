#pragma once

#include "IRenderer.h"
#include "scene/RainSystem.h"
#include "VertexArray.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

class Shader;

// 闪电本体(动态三角带)与全屏闪光(additive)的渲染器。
// 刻意不走实例化路径(文档化例外):雷线网格每帧按分支重建(TRIANGLE_STRIP),
// 闪光用空 VAO 画 3 顶点——因此不继承 InstancedQuadRenderer,直接实现 IRenderer。
class LightningRenderer : public IRenderer
{
public:
    LightningRenderer() = default;
    ~LightningRenderer() = default;

    void Init(std::shared_ptr<RainSystem> rain);

    // 只响应 WorldSlot::Lightning;其余槽位调用为空操作
    void Render(const RenderContext& ctx, WorldSlot slot) override;

    std::vector<WorldSlot> Slots() const override { return { WorldSlot::Lightning }; }

private:
    void BuildBranchMesh(const LightningBranch& branch, std::vector<float>& vertices);
    void RenderBranch(const LightningBranch& branch);

    std::shared_ptr<RainSystem> rain_;

    std::shared_ptr<Shader> lightningShader_;
    std::shared_ptr<Shader> flashShader_;

    VertexArray lightningVao_;
    VertexBuffer lightningVbo_;
    VertexArray flashVao_;
};
