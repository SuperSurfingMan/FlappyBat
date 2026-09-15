#pragma once
// =============================================================================
// UI 设计系统的统一常量与辅助函数
// 适配窗口 540 x 960；所有 UI 颜色、尺寸、字号集中在此维护
// =============================================================================
#include "imgui.h"

namespace UITheme {

    // 轻量 constexpr 颜色类型（Vec3 非 literal type，无法 constexpr）
    struct Color3 {
        float r, g, b;
    };

    // ===== 色彩 token =====
    constexpr Color3 NightBg   = {0.102f, 0.086f, 0.149f}; // #1A1626
    constexpr Color3 NightDeep = {0.059f, 0.047f, 0.094f}; // #0F0C18
    constexpr Color3 Amber     = {0.941f, 0.722f, 0.314f}; // #F0B850
    constexpr Color3 AmberDim  = {0.541f, 0.416f, 0.184f}; // #8A6A2F
    constexpr Color3 Moonlight = {0.749f, 0.784f, 0.910f}; // #BFC8E8
    constexpr Color3 Blood     = {0.765f, 0.271f, 0.271f}; // #C34545

    // ===== 字体层级 =====
    constexpr float FontH1     = 48.0f; // 主标题（艺术字图替代）
    constexpr float FontH2     = 36.0f; // 面板标题
    constexpr float FontButton = 26.0f; // 按钮
    constexpr float FontBody   = 26.0f; // 榜单行
    constexpr float FontHUD    = 32.0f; // 分数 HUD
    constexpr float FontTiny   = 18.0f; // 副标

    // ===== 图形尺寸 =====
    constexpr float IconBtnSize   = 56.0f; // 游戏内顶部图标按钮
    constexpr float MainBtnW      = 320.0f;
    constexpr float MainBtnH      = 92.0f;
    constexpr float SecondaryBtnW = 280.0f;
    constexpr float SecondaryBtnH = 72.0f;
    constexpr float CornerRadius  = 18.0f;
    constexpr float BorderWidth   = 3.0f;

    // ===== 动画 =====
    constexpr float HoverScale   = 1.06f;
    constexpr float PressedScale = 0.95f;
    constexpr float PressedOffsetY = 4.0f;

    // ===== ImGui 颜色辅助 =====
    inline ImU32 ToImU32(const Color3& c, float a = 1.0f) {
        return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, a));
    }

    // 线性插值两个 ImU32（用于 hover 过渡）
    inline ImU32 LerpImU32(ImU32 a, ImU32 b, float t) {
        ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
        ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
        ImVec4 out;
        out.x = ca.x + (cb.x - ca.x) * t;
        out.y = ca.y + (cb.y - ca.y) * t;
        out.z = ca.z + (cb.z - ca.z) * t;
        out.w = ca.w + (cb.w - ca.w) * t;
        return ImGui::GetColorU32(out);
    }
}
