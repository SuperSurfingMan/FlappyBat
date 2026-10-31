#include "scene/stage/FeedbackFx.h"
#include "core/Config.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

void FeedbackFx::Reset() {
    floatingTexts.clear();
    shakeTimer = 0.0f;
    shakeIntensity = 0.0f;
    shakeClock = 0.0f;
}

void FeedbackFx::Spawn(const std::string& text, Vec2 worldPos,
                       float r, float g, float b, float fontSize, float life) {
    floatingTexts.push_back({text, worldPos, r, g, b, fontSize, life, life});
    if (floatingTexts.size() > 16) {
        floatingTexts.erase(floatingTexts.begin()); // 防止极端情况堆积
    }
}

void FeedbackFx::Update(float dt) {
    for (auto& ft : floatingTexts) {
        ft.life -= dt;
        ft.worldPos.y += 55.0f * dt; // 缓慢上浮
    }
    floatingTexts.erase(
        std::remove_if(floatingTexts.begin(), floatingTexts.end(),
                       [](const FloatText& ft) { return ft.life <= 0.0f; }),
        floatingTexts.end());

    if (shakeTimer > 0.0f) {
        shakeTimer = std::max(0.0f, shakeTimer - dt);
        shakeClock += dt;
    }
}

void FeedbackFx::Shake(float intensity) {
    shakeTimer = ShakeDuration;
    shakeIntensity = intensity;
    shakeClock = 0.0f;
}

bool FeedbackFx::ShakeOffset(float& offX, float& offY) const {
    if (shakeTimer <= 0.0f) return false;
    float k = shakeTimer / ShakeDuration; // 1->0 衰减
    float amp = shakeIntensity * k * k;
    offX = amp * std::sin(shakeClock * 71.0f);
    offY = amp * std::cos(shakeClock * 53.0f);
    return true;
}

void FeedbackFx::Draw() const {
    if (floatingTexts.empty()) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    for (const auto& ft : floatingTexts) {
        float alpha = std::clamp(ft.life / ft.totalLife, 0.0f, 1.0f);
        ImVec2 ts = UIBuilder::CalcTextSize(ft.text, ft.fontSize);
        float sx = ft.worldPos.x;              // 世界 x 与屏幕 x 一致
        float sy = WIN_HEIGHT - ft.worldPos.y; // 世界 y 向上，屏幕 y 向下
        ImVec2 pos(sx - ts.x * 0.5f, sy - ts.y * 0.5f);
        dl->AddText(font, ft.fontSize, ImVec2(pos.x + 2, pos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * alpha)), ft.text.c_str());
        dl->AddText(font, ft.fontSize, pos,
                    ImGui::GetColorU32(ImVec4(ft.r, ft.g, ft.b, alpha)), ft.text.c_str());
    }
}
