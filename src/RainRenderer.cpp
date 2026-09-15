#include "RainRenderer.h"

#include "Shader.h"
#include "ShaderLibrary.h"
#include "SpriteRenderer.h"
#include "Config.h"
#include "RenderState.h"

#include <cmath>
#include <algorithm>

void RainRenderer::Init(std::shared_ptr<RainSystem> rain)
{
    rain_ = std::move(rain);
    shader_ = ShaderLibrary::Instance().Get("rain");

    float sw = (float)Config::GetInstance().GetScreenWidth();
    float sh = (float)Config::GetInstance().GetScreenHeight();
    Ortho(0.0f, sw, 0.0f, sh, projection_.m);

    float quad[] =
    {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,

        -0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f
    };

    vao_.Bind();

    // quad（2 分量顶点）
    quadVbo_.BufferData(quad, sizeof(quad), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // 实例缓冲
    instanceVbo_.BufferData(nullptr, 10000 * sizeof(RainInstance), GL_DYNAMIC_DRAW);

    // position
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RainInstance), (void*)0);
    glVertexAttribDivisor(1, 1);

    // size
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RainInstance), (void*)(sizeof(Vec2)));
    glVertexAttribDivisor(2, 1);

    // rotation
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RainInstance), (void*)(sizeof(Vec2) * 2));
    glVertexAttribDivisor(3, 1);

    // mask_height
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RainInstance), (void*)(sizeof(Vec2) * 2 + sizeof(float)));
    glVertexAttribDivisor(4, 1);

    vao_.Unbind();
}

void RainRenderer::Render(SpriteRenderer& spriteRenderer)
{
    // 兼容入口：按层序在当前调用处整批绘制（MainScene 使用 RenderDepthLayer 穿插场景层级）
    for (int band = 0; band < DepthBandCount; ++band) {
        RenderDepthLayer(spriteRenderer, band);
    }
}

int RainRenderer::DepthBandOf(float depth) {
    if (depth < 0.25f) return 0; // 最底层：垫在背景之下
    if (depth < 0.50f) return 1; // 第一层远景
    if (depth < 0.75f) return 2; // 第二层远景（水花所在层）
    return 3;                    // 近景：障碍与蝙蝠之前
}

float RainRenderer::BandAlpha(int band) {
    switch (band) {
        case 0:  return 0.28f; // 最底层最淡，仅营造纵深
        case 1:  return 0.45f;
        case 2:  return 0.65f;
        default: return 0.95f; // 近景最实
    }
}

void RainRenderer::RenderDepthLayer(SpriteRenderer& spriteRenderer, int band)
{
    if (!rain_ || !shader_ || band < 0 || band >= DepthBandCount)
        return;

    const auto& drops = rain_->GetDrops();
    // 仅绘制当前雨强激活的雨滴（A4-c）
    const size_t activeCount = std::min(rain_->GetActiveDropCount(), drops.size());

    // 1. 逻辑数据 -> 渲染实例数据（只收集属于该层的雨滴）
    instances_.clear();
    const float wind = rain_->GetWind();
    const float rainWidth = rain_->GetRainWidth();
    for (size_t i = 0; i < activeCount; ++i)
    {
        const RainDrop& drop = drops[i];
        if (DepthBandOf(drop.depth) != band) continue;

        RainInstance& inst = instances_.emplace_back();
        const float scale = Lerp(0.4f, 1.6f, drop.depth); // 近景粗、远景细（与分层配合强化纵深）
        inst.position = drop.position;
        inst.size = Vec2(rainWidth * scale, drop.length);
        inst.rotation = (wind != 0.0f) ? atan(wind / drop.speed) : 0.0f;
        inst.maskHeight = drop.maskHeight;
    }

    // 2. 实例化绘制该层雨滴（带分层透明度）
    if (!instances_.empty())
    {
        BlendScope blend(BlendMode::Alpha());

        shader_->Use();
        shader_->SetMat4("uProjection", projection_.m);
        shader_->SetVec3("uColor", 0.9f, 0.95f, 1.0f);
        shader_->SetFloat("uGlow", 1.0f);
        shader_->SetFloat("uLightning", rain_->GetLightningExposure());
        shader_->SetFloat("uAlpha", BandAlpha(band));

        vao_.Bind();
        instanceVbo_.BufferSubData(instances_.data(), instances_.size() * sizeof(RainInstance), 0);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)instances_.size());
        vao_.Unbind();
    }

    // 3. 贴地水花跟随第二层远景（地面之上、障碍之下）
    if (band == 2) {
        for (const auto& splash : rain_->GetActiveSplashes())
            spriteRenderer.Draw_UV(splash);
    }
}
