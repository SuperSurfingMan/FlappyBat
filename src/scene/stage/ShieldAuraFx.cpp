#include "scene/stage/ShieldAuraFx.h"
#include "ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
constexpr UITheme::Color3 kIce{0.55f, 0.84f, 1.0f};
// 遮罩相对蝙蝠碰撞圆的外扩量：只比蝙蝠大一圈，避免半透明球体糊住周围赛道
constexpr float kAuraPadding = 10.0f;
}

void DrawShieldAura(float screenH, Vec2 batWorldPos, float batCollisionRadius,
                    float remaining, float duration) {
    if (remaining <= 0.0f || duration <= 0.0f || batCollisionRadius <= 0.0f) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    const float radius = batCollisionRadius + kAuraPadding; // 实际绘制半径
    const ImVec2 c(batWorldPos.x, screenH - batWorldPos.y); // 世界 y 向上 -> 屏幕 y 向下
    const float age  = std::max(0.0f, duration - remaining);
    const float appear = std::clamp(age / 0.06f, 0.0f, 1.0f);
    const float vanish = std::clamp(remaining / 0.12f, 0.0f, 1.0f);
    const float a = appear * vanish; // 起手淡入 + 尾段淡出

    // 1) 释放瞬间的扩散冲击环：贴着外壳轻微外扩后淡出（不向外炸开成大圈）
    const float wave = std::clamp(age / 0.28f, 0.0f, 1.0f);
    if (wave < 1.0f) {
        dl->AddCircle(c, radius * (1.0f + 0.35f * wave),
                      UITheme::ToImU32(kIce, 0.5f * (1.0f - wave) * a),
                      48, 1.0f + 3.0f * (1.0f - wave));
    }

    // 2) 三层同心填充近似径向渐变（外淡内实），随时间轻微呼吸
    const float r = radius * (1.0f + 0.045f * std::sin(age * 22.0f));
    dl->AddCircleFilled(c, r * 1.06f, UITheme::ToImU32(kIce, 0.07f * a), 56);
    dl->AddCircleFilled(c, r * 0.86f, UITheme::ToImU32(kIce, 0.10f * a), 56);
    dl->AddCircleFilled(c, r * 0.62f, UITheme::ToImU32(UITheme::Moonlight, 0.10f * a), 48);

    // 3) 主边界环 + 内圈细环
    dl->AddCircle(c, r,        UITheme::ToImU32(kIce, 0.75f * a), 64, 2.5f);
    dl->AddCircle(c, r * 0.9f, UITheme::ToImU32(UITheme::Moonlight, 0.30f * a), 64, 1.2f);

    // 4) 两段旋转弧 + 环绕光点：让 0.5s 内始终有可见动态
    const float spin = age * 7.5f;
    for (int i = 0; i < 2; ++i) {
        const float a0 = spin + i * kPi;
        dl->PathArcTo(c, r * 1.13f, a0, a0 + 0.85f, 24);
        dl->PathStroke(UITheme::ToImU32(UITheme::Amber, 0.85f * a), false, 3.0f);
    }
    for (int i = 0; i < 6; ++i) {
        const float ang = spin * 0.6f + i * (2.0f * kPi / 6.0f);
        dl->AddCircleFilled(
            ImVec2(c.x + std::cos(ang) * r * 1.02f, c.y + std::sin(ang) * r * 1.02f),
            2.2f, UITheme::ToImU32(UITheme::Moonlight, 0.7f * a), 8);
    }
}
