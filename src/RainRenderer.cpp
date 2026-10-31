#include "RainRenderer.h"

#include "Shader.h"
#include "ShaderLibrary.h"
#include "SpriteRenderer.h"
#include "Config.h"
#include "RenderState.h"

#include <cmath>
#include <algorithm>

namespace {

// 雨带槽位 -> depth band 下标(渲染器内部映射,WorldSlot 不感知雨的细节)
int BandOfSlot(WorldSlot slot) {
    switch (slot) {
        case WorldSlot::RainFar:    return 0;
        case WorldSlot::RainMid:    return 1;
        case WorldSlot::RainGround: return 2;
        case WorldSlot::RainNear:   return 3;
        default:                    return -1;
    }
}

// 实例缓冲容量(按全部雨滴规模预留)
constexpr size_t kMaxRainInstances = 10000;

} // namespace

void RainRenderer::Init(std::shared_ptr<RainSystem> rain)
{
    rain_ = std::move(rain);
    shader_ = ShaderLibrary::Instance().Get("rain");

    InitInstancedQuad(kMaxRainInstances, sizeof(RainInstance));

    // position -> size -> rotation -> mask_height(location 1~4)
    size_t offset = 0;
    DefineInstanceAttrib({ 1, 2, offset }); offset += sizeof(Vec2);
    DefineInstanceAttrib({ 2, 2, offset }); offset += sizeof(Vec2);
    DefineInstanceAttrib({ 3, 1, offset }); offset += sizeof(float);
    DefineInstanceAttrib({ 4, 1, offset });
}

int RainRenderer::DepthBandOf(float depth) {
    if (depth < 0.25f) return 0; // 最底层:垫在背景之下
    if (depth < 0.50f) return 1; // 第一层远景
    if (depth < 0.75f) return 2; // 第二层远景(水花所在层)
    return 3;                    // 近景:障碍与蝙蝠之前
}

float RainRenderer::BandAlpha(int band) {
    switch (band) {
        case 0:  return 0.28f; // 最底层最淡,仅营造纵深
        case 1:  return 0.45f;
        case 2:  return 0.65f;
        default: return 0.95f; // 近景最实
    }
}

void RainRenderer::Render(const RenderContext& ctx, WorldSlot slot)
{
    const int band = BandOfSlot(slot);
    if (band >= 0)
        RenderDepthLayer(ctx, band);
}

void RainRenderer::RenderDepthLayer(const RenderContext& ctx, int band)
{
    if (!rain_ || !shader_ || band < 0 || band >= DepthBandCount)
        return;

    const auto& drops = rain_->GetDrops();
    // 仅绘制当前雨强激活的雨滴(A4-c)
    const size_t activeCount = std::min(rain_->GetActiveDropCount(), drops.size());

    // 1. 逻辑数据 -> 渲染实例数据(只收集属于该层的雨滴)
    instances_.clear();
    const float wind = rain_->GetWind();
    const float rainWidth = rain_->GetRainWidth();
    for (size_t i = 0; i < activeCount; ++i)
    {
        const RainDrop& drop = drops[i];
        if (DepthBandOf(drop.depth) != band) continue;

        RainInstance& inst = instances_.emplace_back();
        const float scale = Lerp(0.4f, 1.6f, drop.depth); // 近景粗、远景细(与分层配合强化纵深)
        inst.position = drop.position;
        inst.size = Vec2(rainWidth * scale, drop.length);
        inst.rotation = (wind != 0.0f) ? atan(wind / drop.speed) : 0.0f;
        inst.maskHeight = drop.maskHeight;
    }

    // 2. 实例化绘制该层雨滴(带分层透明度)
    if (!instances_.empty())
    {
        BlendScope blend(BlendMode::Alpha());

        shader_->Use();
        shader_->SetMat4("uProjection", ctx.projection.Data());
        shader_->SetVec3("uColor", 0.9f, 0.95f, 1.0f);
        shader_->SetFloat("uGlow", 1.0f);
        shader_->SetFloat("uLightning", rain_->GetLightningExposure());
        shader_->SetFloat("uAlpha", BandAlpha(band));

        UploadInstances(instances_.data(), instances_.size() * sizeof(RainInstance));
        DrawInstanced(instances_.size());
    }

    // 3. 贴地水花跟随第二层远景(地面之上、障碍之下)
    if (band == 2) {
        for (const auto& splash : rain_->GetActiveSplashes())
            ctx.sprites.Draw_UV(splash);
    }
}
