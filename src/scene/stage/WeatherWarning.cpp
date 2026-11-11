#include "scene/stage/WeatherWarning.h"

#include "scene/stage/RainSystem.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "core/Config.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace weather {
namespace {

// 沿折线画半透明虚线（屏幕坐标）：按弧长交替 dashOn/dashOff
void DrawDashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col,
                        float thickness, float dashOn, float dashOff) {
    bool drawing = true;
    float budget = dashOn;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        ImVec2 a = pts[i], b = pts[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        ImVec2 dir(dx / len, dy / len);
        float s = 0.0f;
        while (s < len - 1e-4f) {
            float step = std::min(budget, len - s);
            if (drawing) {
                dl->AddLine(ImVec2(a.x + dir.x * s, a.y + dir.y * s),
                            ImVec2(a.x + dir.x * (s + step), a.y + dir.y * (s + step)),
                            col, thickness);
            }
            s += step;
            budget -= step;
            if (budget <= 1e-4f) {
                drawing = !drawing;
                budget = drawing ? dashOn : dashOff;
            }
        }
    }
}

} // namespace

float DistancePointSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float abLenSq = ab.x * ab.x + ab.y * ab.y;
    float t = 0.0f;
    if (abLenSq > 1e-8f) {
        t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / abLenSq, 0.0f, 1.0f);
    }
    float dx = p.x - (a.x + ab.x * t);
    float dy = p.y - (a.y + ab.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

void DrawLightningWarningOverlay(const RainSystem& rain, const Config& cfg, float pulseClock) {
    if (rain.GetLightningPhase() != RainSystem::LightningPhase::Warning) return;

    const auto& pts = rain.GetLightning().mainBranch.points;
    if (pts.size() < 2) return;

    const float SCREEN_WIDTH = (float)cfg.GetScreenWidth();
    const float SCREEN_HEIGHT = (float)cfg.GetScreenHeight();

    // 折线（世界坐标，y 向上）转屏幕坐标（y 向下）
    std::vector<ImVec2> screenPts;
    screenPts.reserve(pts.size());
    for (const auto& p : pts) {
        screenPts.emplace_back(p.x, SCREEN_HEIGHT - p.y);
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float pulse = 0.5f + 0.5f * std::sin(pulseClock * 14.0f);
    float progress = 1.0f - rain.GetWarningTimer() / rain.GetLightningWarnDuration(); // 0->1 越接近落雷越醒目

    // 1) 粗范围矩形：雷可能漂移的整个区域（落点 ±漂移钳制，全高），比虚线更淡
    float sx = rain.GetStrikeX();
    float zoneHalf = RainSystem::StrikeDriftClamp;
    float rectAlpha = (0.08f + 0.14f * pulse) * (0.5f + 0.5f * progress);
    dl->AddRectFilledMultiColor(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)));
    dl->AddRect(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        UITheme::ToImU32(UITheme::Amber, 0.25f + 0.35f * pulse), 4.0f, 0, 2.0f);

    // 2) 精确路径：沿主分支折线的半透明虚线（实际雷线将劈到的位置）
    float alpha = (0.30f + 0.25f * pulse) * (0.5f + 0.5f * progress);
    DrawDashedPolyline(dl, screenPts, UITheme::ToImU32(UITheme::Amber, alpha),
                       5.0f + 2.0f * progress, 16.0f, 11.0f);

    // 3) 落点标记：「雷」字（水平钳制在窗口内，避免越界）
    ImVec2 ts = UIBuilder::CalcTextSize("雷", 24.0f);
    float halfW = ts.x * 0.5f;
    float textX = std::clamp(sx, halfW + 8.0f, SCREEN_WIDTH - halfW - 8.0f);
    UIBuilder::DrawCenteredText("雷", textX,
                                SCREEN_HEIGHT - cfg.GetTerrainHeight() - 22.0f,
                                24.0f, UITheme::ToImU32(UITheme::Amber, 0.5f + 0.5f * pulse));
}

} // namespace weather
