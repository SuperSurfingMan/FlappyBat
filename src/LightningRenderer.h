#pragma once

#include "IRenderer.h"
#include "scene/RainSystem.h"
#include "VertexArray.h"
#include "VertexBuffer.h"
#include "Math.h"

#include <memory>
#include <vector>

class Shader;
struct LightningBranch;

// 闪电本体（动态三角带）与全屏闪光（additive）的渲染器。
class LightningRenderer : public IRenderer
{
public:
    LightningRenderer() = default;
    ~LightningRenderer() = default;

    void Init(std::shared_ptr<RainSystem> rain);

    void Render(SpriteRenderer& spriteRenderer) override;

private:
    void BuildBranchMesh(const LightningBranch& branch, std::vector<float>& vertices);
    void RenderBranch(const LightningBranch& branch);

    std::shared_ptr<RainSystem> rain_;

    std::shared_ptr<Shader> lightningShader_;
    std::shared_ptr<Shader> flashShader_;

    VertexArray lightningVao_;
    VertexBuffer lightningVbo_;
    VertexArray flashVao_;

    Mat4 lightningProjection_;
};
