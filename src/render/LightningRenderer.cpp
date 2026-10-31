#include "render/LightningRenderer.h"

#include "render/Shader.h"
#include "render/ShaderLibrary.h"
#include "core/Config.h"
#include "render/RenderState.h"

void LightningRenderer::Init(std::shared_ptr<RainSystem> rain)
{
    rain_ = std::move(rain);
    lightningShader_ = ShaderLibrary::Instance().Get("lightning");
    flashShader_ = ShaderLibrary::Instance().Get("flash");

    lightningVao_.Bind();
    lightningVbo_.Bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);
    lightningVao_.Unbind();
    // flashVao_ 为空 VAO(core profile 绘制需要绑定一个 VAO)
}

void LightningRenderer::Render(const RenderContext& ctx, WorldSlot slot)
{
    if (slot != WorldSlot::Lightning) return;
    if (!rain_ || !rain_->IsLightning())
        return;

    // 1. 闪电本体(additive)
    if (lightningShader_)
    {
        BlendScope blend(BlendMode::Additive());

        lightningShader_->Use();
        lightningShader_->SetMat4("uProjection", ctx.projection.Data());
        lightningShader_->SetFloat("uTime", rain_->GetLightningTimeCounter());

        const Lightning& lightning = rain_->GetLightning();
        RenderBranch(lightning.mainBranch);
        for (const auto& b : lightning.subBranches)
            RenderBranch(b);
    }

    // 2. 全屏闪光(additive)
    if (flashShader_ && rain_->GetLightningExposure() > 0.001f)
    {
        BlendScope blend(BlendMode::Additive());

        flashShader_->Use();
        flashShader_->SetFloat("uExposure", rain_->GetLightningExposure());

        flashVao_.Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        flashVao_.Unbind();
    }
}

void LightningRenderer::BuildBranchMesh(const LightningBranch& branch, std::vector<float>& vertices)
{
    vertices.clear();

    for (size_t i = 0; i < branch.points.size(); ++i)
    {
        Vec2 p = branch.points[i];

        Vec2 dir;
        if (i == branch.points.size() - 1)
            dir = branch.points[i] - branch.points[i - 1];
        else
            dir = branch.points[i + 1] - branch.points[i];

        dir.Normalize();
        Vec2 normal(-dir.y, dir.x);

        float t = (float)i / (branch.points.size() - 1);
        float width = branch.thickness * (1.8f * (1.0f - t * t) + 0.3f);
        Vec2 left = p - normal * width * 0.5f;
        Vec2 right = p + normal * width * 0.5f;

        vertices.push_back(left.x);
        vertices.push_back(left.y);
        vertices.push_back(-1.0f);

        vertices.push_back(right.x);
        vertices.push_back(right.y);
        vertices.push_back(1.0f);
    }
}

void LightningRenderer::RenderBranch(const LightningBranch& branch)
{
    if (branch.points.size() < 2)
        return;

    std::vector<float> vertices;
    BuildBranchMesh(branch, vertices);

    lightningVao_.Bind();
    lightningVbo_.BufferData(vertices.data(), vertices.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)(vertices.size() / 3));
    lightningVao_.Unbind();
}
