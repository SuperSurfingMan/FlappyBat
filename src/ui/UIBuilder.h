#pragma once
// =============================================================================
// 基于 ImGui DrawList 的 UI 程序化绘制原语
// 用于按钮（圆角矩形底板+描边+高光+文字）以及其他 UI 元素
// 所有按钮/图标几何形状简单, 完全由代码绘制以保证风格统一
// =============================================================================
#include "imgui.h"
#include "UITheme.h"
#include "../font/FontManager.h"
#include <string>
#include <cfloat>

namespace UIBuilder {

    enum class IconKind {
        Pause,
        Play,
        Home,
        Quit,
        Restart,
        Rank
    };

    // ===== 取中文字体（统一入口） =====
    inline ImFont* GetFont() {
        return FontManager::Instance().GetFont(FontManager::FontType::VonwaonBitmap16);
    }

    // ===== 计算指定字号下的文本尺寸 =====
    inline ImVec2 CalcTextSize(const std::string& text, float fontSize) {
        ImFont* font = GetFont();
        if (!font) return ImVec2(fontSize * 0.5f * (float)text.size(), fontSize);
        return font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, text.c_str());
    }

    // ===== 阴影椭圆（按钮下方） =====
    inline void DrawShadow(float cx, float cy, float w, float h, float alpha = 0.55f) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 pmin(cx - w * 0.5f, cy - h * 0.5f);
        ImVec2 pmax(cx + w * 0.5f, cy + h * 0.5f);
        dl->AddRectFilled(pmin, pmax, ImGui::GetColorU32(ImVec4(0, 0, 0, alpha)), h * 0.5f);
    }

    // ===== 圆角矩形 =====
    inline void DrawRoundedRect(float cx, float cy, float w, float h, float radius,
                                ImU32 fill, ImU32 border, float borderWidth = UITheme::BorderWidth)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 pmin(cx - w * 0.5f, cy - h * 0.5f);
        ImVec2 pmax(cx + w * 0.5f, cy + h * 0.5f);
        dl->AddRectFilled(pmin, pmax, fill, radius);
        if (borderWidth > 0) {
            dl->AddRect(pmin, pmax, border, radius, 0, borderWidth);
        }
    }

    // ===== 带顶部高光的圆角矩形（模拟立体石板） =====
    inline void DrawButtonPlate(float cx, float cy, float w, float h, float radius,
                                ImU32 fillTop, ImU32 fillBottom, ImU32 border,
                                float borderWidth = UITheme::BorderWidth)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 pmin(cx - w * 0.5f, cy - h * 0.5f);
        ImVec2 pmax(cx + w * 0.5f, cy + h * 0.5f);

        // 圆角半径 clamp（避免超过按钮宽/高的一半导致形状异常）
        float r = radius;
        if (r * 2.0f > h) r = h * 0.5f;
        if (r * 2.0f > w) r = w * 0.5f;

        // 三段式垂直渐变（顶部亮 -> 底部暗），每一段都正确保留圆角：
        //   顶部圆角帽（单色 fillTop）+ 中间直角渐变区 + 底部圆角帽（单色 fillBottom）
        if (r < 1.0f) {
            dl->AddRectFilledMultiColor(pmin, pmax, fillTop, fillTop, fillBottom, fillBottom);
        } else {
            dl->AddRectFilled(pmin, ImVec2(pmax.x, pmin.y + r), fillTop, r, ImDrawFlags_RoundCornersTop);
            ImU32 topCol    = UITheme::LerpImU32(fillTop, fillBottom, r / h);
            ImU32 bottomCol = UITheme::LerpImU32(fillTop, fillBottom, 1.0f - r / h);
            dl->AddRectFilledMultiColor(
                ImVec2(pmin.x, pmin.y + r), ImVec2(pmax.x, pmax.y - r),
                topCol, topCol, bottomCol, bottomCol);
            dl->AddRectFilled(ImVec2(pmin.x, pmax.y - r), pmax, fillBottom, r, ImDrawFlags_RoundCornersBottom);
        }

        // 顶部高光
        dl->AddLine(ImVec2(pmin.x + r, pmin.y + 2),
                    ImVec2(pmax.x - r, pmin.y + 2),
                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f)), 1.5f);
        // 描边
        dl->AddRect(pmin, pmax, border, r, 0, borderWidth);
    }

    // ===== 程序化按钮底板 =====
    //   isPrimary: true 主按钮（暗紫→深紫底色+亮金描边）, false 次级按钮（更暗+暗金描边）
    //   isDanger:  危险按钮（暗红底色+血红描边，用于清空记录等破坏性操作）
    inline void DrawButton(float cx, float cy, float w, float h, bool isPrimary,
                           bool isHover, bool isPressed, bool isDanger = false)
    {
        ImU32 fillTopA = isPrimary ? UITheme::ToImU32({0.243f, 0.169f, 0.396f})
                                   : UITheme::ToImU32({0.149f, 0.122f, 0.227f});
        ImU32 fillTopB = isPrimary ? UITheme::ToImU32({0.180f, 0.114f, 0.310f})
                                   : UITheme::ToImU32({0.090f, 0.071f, 0.149f});

        if (isDanger) {
            fillTopA = UITheme::ToImU32({0.310f, 0.106f, 0.125f});
            fillTopB = UITheme::ToImU32({0.173f, 0.055f, 0.075f});
        }

        ImU32 border = isHover ? (isDanger ? UITheme::ToImU32({0.950f, 0.435f, 0.435f})
                                           : UITheme::ToImU32(UITheme::Amber))
                               : (isDanger ? UITheme::ToImU32(UITheme::Blood)
                                           : UITheme::ToImU32(UITheme::AmberDim));
        float borderW = isHover ? 4.0f : UITheme::BorderWidth;
        float alpha = isPressed ? 0.85f : 1.0f;

        // 应用整体透明度
        {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(fillTopA); c.w *= alpha; fillTopA = ImGui::GetColorU32(c);
            ImVec4 c2 = ImGui::ColorConvertU32ToFloat4(fillTopB); c2.w *= alpha; fillTopB = ImGui::GetColorU32(c2);
            ImVec4 c3 = ImGui::ColorConvertU32ToFloat4(border); c3.w *= alpha; border = ImGui::GetColorU32(c3);
        }

        DrawShadow(cx, cy + h * 0.4f, w * 0.95f, h * 0.4f, 0.45f * alpha);
        DrawButtonPlate(cx, cy, w, h, UITheme::CornerRadius, fillTopA, fillTopB, border, borderW);
    }

    // ===== 居中绘制按钮文字（带阴影） =====
    inline void DrawButtonText(const std::string& text, float cx, float cy,
                               float fontSize, bool isHover, bool isPressed, bool isDanger = false)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = GetFont();
        if (!font) return;

        ImVec2 ts = CalcTextSize(text, fontSize);
        ImVec2 pos(cx - ts.x * 0.5f, cy - ts.y * 0.5f);

        ImU32 textColor = isHover ? (isDanger ? UITheme::ToImU32({0.950f, 0.480f, 0.480f})
                                              : UITheme::ToImU32(UITheme::Amber))
                                  : (isDanger ? UITheme::ToImU32({0.855f, 0.415f, 0.415f})
                                              : UITheme::ToImU32(UITheme::Moonlight));
        if (isPressed) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(textColor);
            c.w *= 0.85f;
            textColor = ImGui::GetColorU32(c);
        }
        ImU32 shadowColor = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f));

        dl->AddText(font, fontSize, ImVec2(pos.x + 2, pos.y + 2), shadowColor, text.c_str());
        dl->AddText(font, fontSize, pos, textColor, text.c_str());
    }

    // ===== 图标底板（深色圆角方形） =====
    inline void DrawIconBackground(float cx, float cy, float s, bool isHover, bool isPressed) {
        ImU32 fillTop = UITheme::ToImU32({0.243f, 0.169f, 0.396f});
        ImU32 fillBottom = UITheme::ToImU32({0.180f, 0.114f, 0.310f});
        ImU32 border = isHover ? UITheme::ToImU32(UITheme::Amber) : UITheme::ToImU32(UITheme::AmberDim);
        if (isPressed) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(fillTop); c.w *= 0.85f; fillTop = ImGui::GetColorU32(c);
            ImVec4 c2 = ImGui::ColorConvertU32ToFloat4(fillBottom); c2.w *= 0.85f; fillBottom = ImGui::GetColorU32(c2);
        }
        DrawShadow(cx, cy + s * 0.3f, s * 0.9f, s * 0.3f, 0.4f);
        DrawButtonPlate(cx, cy, s, s, s * 0.25f, fillTop, fillBottom, border, 2.5f);
    }

    // ===== 暂停：两条竖线 =====
    inline void DrawIconPause(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float w = s * 0.14f;
        float h = s * 0.45f;
        float gap = s * 0.14f;
        dl->AddRectFilled(ImVec2(cx - gap - w + 1, cy - h * 0.5f + 1), ImVec2(cx - gap + 1, cy + h * 0.5f + 1), glow, 3.0f);
        dl->AddRectFilled(ImVec2(cx + gap - w + 1, cy - h * 0.5f + 1), ImVec2(cx + gap + 1, cy + h * 0.5f + 1), glow, 3.0f);
        dl->AddRectFilled(ImVec2(cx - gap - w, cy - h * 0.5f), ImVec2(cx - gap, cy + h * 0.5f), gold, 3.0f);
        dl->AddRectFilled(ImVec2(cx + gap - w, cy - h * 0.5f), ImVec2(cx + gap, cy + h * 0.5f), gold, 3.0f);
    }

    // ===== 播放/继续：三角形 =====
    inline void DrawIconPlay(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float h = s * 0.5f;
        float w = s * 0.45f;
        dl->AddTriangleFilled(
            ImVec2(cx - w * 0.4f + 1, cy - h * 0.5f + 1),
            ImVec2(cx - w * 0.4f + 1, cy + h * 0.5f + 1),
            ImVec2(cx + w * 0.6f + 1, cy + 1), glow);
        dl->AddTriangleFilled(
            ImVec2(cx - w * 0.4f, cy - h * 0.5f),
            ImVec2(cx - w * 0.4f, cy + h * 0.5f),
            ImVec2(cx + w * 0.6f, cy), gold);
    }

    // ===== 返回主页：房子 =====
    inline void DrawIconHome(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float w = s * 0.55f;
        float h = s * 0.5f;
        float roofH = h * 0.5f;
        // 阴影
        dl->AddQuadFilled(
            ImVec2(cx - w * 0.5f + 1, cy - roofH * 0.2f + 1),
            ImVec2(cx + 1, cy - h * 0.5f - roofH * 0.2f + 1),
            ImVec2(cx + w * 0.5f + 1, cy - roofH * 0.2f + 1),
            ImVec2(cx + w * 0.5f + 1, cy + h * 0.5f + 1), glow);
        dl->AddQuadFilled(
            ImVec2(cx + w * 0.5f + 1, cy + h * 0.5f + 1),
            ImVec2(cx - w * 0.5f + 1, cy + h * 0.5f + 1),
            ImVec2(cx - w * 0.5f + 1, cy - roofH * 0.2f + 1),
            ImVec2(cx - w * 0.5f + 1, cy - roofH * 0.2f + 1), glow);
        // 主体（屋顶 + 墙）
        dl->AddQuadFilled(
            ImVec2(cx - w * 0.5f, cy - roofH * 0.2f),
            ImVec2(cx, cy - h * 0.5f - roofH * 0.2f),
            ImVec2(cx + w * 0.5f, cy - roofH * 0.2f),
            ImVec2(cx + w * 0.5f, cy + h * 0.5f), gold);
        dl->AddQuadFilled(
            ImVec2(cx + w * 0.5f, cy + h * 0.5f),
            ImVec2(cx - w * 0.5f, cy + h * 0.5f),
            ImVec2(cx - w * 0.5f, cy - roofH * 0.2f),
            ImVec2(cx - w * 0.5f, cy - roofH * 0.2f), gold);
    }

    // ===== 退出：门 + 把手 =====
    inline void DrawIconQuit(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float w = s * 0.42f;
        float h = s * 0.6f;
        dl->AddRect(ImVec2(cx - w * 0.5f + 1, cy - h * 0.5f + 1),
                    ImVec2(cx + w * 0.5f + 1, cy + h * 0.5f + 1), glow, 4.0f, 0, 4.0f);
        dl->AddRect(ImVec2(cx - w * 0.5f, cy - h * 0.5f),
                    ImVec2(cx + w * 0.5f, cy + h * 0.5f), gold, 4.0f, 0, 4.0f);
        dl->AddCircleFilled(ImVec2(cx + w * 0.25f + 1, cy + 1), 2.5f, glow);
        dl->AddCircleFilled(ImVec2(cx + w * 0.25f, cy), 2.5f, gold);
    }

    // ===== 重新开始：循环箭头 =====
    inline void DrawIconRestart(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float r = s * 0.3f;
        const int segments = 24;
        // 光晕弧
        dl->PathArcTo(ImVec2(cx + 1, cy + 1), r, 0.7f, 5.5f, segments);
        dl->PathStroke(glow, 0, 4.0f);
        // 主体弧
        dl->PathArcTo(ImVec2(cx, cy), r, 0.7f, 5.5f, segments);
        dl->PathStroke(gold, 0, 4.0f);
        // 箭头三角
        dl->AddTriangleFilled(
            ImVec2(cx + r * 0.95f + 1, cy - r * 0.15f + 1),
            ImVec2(cx + r * 0.7f + 1, cy - r * 0.6f + 1),
            ImVec2(cx + r * 0.55f + 1, cy + r * 0.15f + 1), glow);
        dl->AddTriangleFilled(
            ImVec2(cx + r * 0.95f, cy - r * 0.15f),
            ImVec2(cx + r * 0.7f, cy - r * 0.6f),
            ImVec2(cx + r * 0.55f, cy + r * 0.15f), gold);
    }

    // ===== 排行榜：奖杯 =====
    inline void DrawIconRank(float cx, float cy, float s, bool isHover, bool isPressed) {
        DrawIconBackground(cx, cy, s, isHover, isPressed);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 gold = UITheme::ToImU32(UITheme::Amber);
        ImU32 glow = UITheme::ToImU32(UITheme::Amber, 0.45f);
        float r = s * 0.18f;
        float cyOff = cy - s * 0.06f;
        // 杯身
        dl->AddCircleFilled(ImVec2(cx + 1, cyOff + 1), r, glow);
        dl->AddCircleFilled(ImVec2(cx, cyOff), r, gold);
        // 杯底
        float baseW = s * 0.4f;
        float baseH = s * 0.12f;
        dl->AddRectFilled(ImVec2(cx - baseW * 0.5f + 1, cyOff + r * 0.7f + 1),
                          ImVec2(cx + baseW * 0.5f + 1, cyOff + r * 0.7f + baseH + 1), glow, 2.0f);
        dl->AddRectFilled(ImVec2(cx - baseW * 0.5f, cyOff + r * 0.7f),
                          ImVec2(cx + baseW * 0.5f, cyOff + r * 0.7f + baseH), gold, 2.0f);
        // 把手
        dl->AddCircle(ImVec2(cx - r * 0.95f + 1, cyOff + 1), r * 0.45f, glow, 12, 3.0f);
        dl->AddCircle(ImVec2(cx + r * 0.95f + 1, cyOff + 1), r * 0.45f, glow, 12, 3.0f);
        dl->AddCircle(ImVec2(cx - r * 0.95f, cyOff), r * 0.45f, gold, 12, 3.0f);
        dl->AddCircle(ImVec2(cx + r * 0.95f, cyOff), r * 0.45f, gold, 12, 3.0f);
    }

    // ===== 居中带阴影文字（通用） =====
    inline void DrawCenteredText(const std::string& text, float cx, float cy,
                                 float fontSize, ImU32 color)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = GetFont();
        if (!font) return;
        ImVec2 ts = CalcTextSize(text, fontSize);
        ImVec2 pos(cx - ts.x * 0.5f, cy - ts.y * 0.5f);
        dl->AddText(font, fontSize, ImVec2(pos.x + 2, pos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), text.c_str());
        dl->AddText(font, fontSize, pos, color, text.c_str());
    }
}
