#include "StageHud.h"
#include "../../Config.h"
#include "../../ui/UIWidget.h"
#include "../../ui/UIBuilder.h"
#include "../../ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// ===== HUD 布局锚点（分数/能量/金币/俯冲）=====
// SCORE：左上角紧邻帧率 overlay（Application 固定 (10,10) 的 ImGui 窗口）下方；
// 金币：帧率 overlay 右侧、与其垂直居中（贴着 FPS 框）；
// 天气消息盒：贴着 SCORE 栏下缘、向下可变高；
// 能量组 + 俯冲长条：窗口底部中央并排一行（能量组在左、俯冲长条在右）。
constexpr float HudLeftX    = 24.0f;   // 左上列起点 x
constexpr float HudScoreW   = 200.0f;  // SCORE 底板宽
constexpr float HudScoreH   = 56.0f;   // SCORE 底板高
constexpr float HudIconSize = 52.0f;   // 能量图标边长
constexpr float HudBarW     = 142.0f;  // 能量槽宽
constexpr float HudBarGap   = 6.0f;    // 图标与槽间距
constexpr float HudCoinsW   = 88.0f;   // 金币胶囊宽
constexpr float HudCoinsH   = 30.0f;   // 金币胶囊高

// 帧率 overlay 几何估算（宽按 "FPS: 8888.8" 最宽情形估，防 fps 位数变化时金币框压住 overlay）
float FpsBoxRightX() {
    const ImGuiStyle& st = ImGui::GetStyle();
    return 10.0f + st.WindowPadding.x * 2.0f + ImGui::CalcTextSize("FPS: 8888.8").x;
}
float FpsBoxBottomY() {
    const ImGuiStyle& st = ImGui::GetStyle();
    return 10.0f + st.WindowPadding.y * 2.0f + ImGui::GetTextLineHeight();
}
// SCORE 底板中心 y：紧邻帧率 overlay 下缘（+2px 间距）
float ScorePanelCy() {
    return FpsBoxBottomY() + 2.0f + HudScoreH * 0.5f;
}

// 俯冲长条几何（窗口底部中央，与能量组并排成一行）
constexpr float DiveBarW = 150.0f;
constexpr float DiveBarH = 32.0f;

// 底部并排组：[能量图标|能量槽] + 间距 + [俯冲长条]，整组水平居中于窗口底部
constexpr float HudGroupGap     = 10.0f; // 能量组与俯冲长条的并排间距
constexpr float HudBottomGroupW = HudIconSize + HudBarGap + HudBarW + HudGroupGap + DiveBarW;
constexpr float HudBottomMargin = 12.0f; // 底部行距窗口底边（以组内最高的能量图标计）
// 底部单行中心 y：全行元素垂直居中
float BottomRowCy() {
    return (float)Config::GetInstance().GetScreenHeight() - HudBottomMargin - HudIconSize * 0.5f;
}

// 沿圆角矩形周长自顶部中点顺时针描边 frac 比例（俯冲长条的冷却环绕动画）
void StrokeRoundedRectPerimeter(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax,
                                float r, float frac, ImU32 col, float thickness) {
    const float w = pmax.x - pmin.x;
    const float h = pmax.y - pmin.y;
    const float straightW = std::max(0.0f, w - 2.0f * r);
    const float straightH = std::max(0.0f, h - 2.0f * r);
    const float arcQ = 1.5707963f * r;
    const float total = 2.0f * (straightW + straightH) + 4.0f * arcQ;
    float budget = total * std::clamp(frac, 0.0f, 1.0f);
    if (budget <= 1e-3f) return;

    const float l = pmin.x, t = pmin.y, rr = pmax.x, b = pmax.y;
    const float cxTop = l + r + straightW * 0.5f;
    ImVec2 prev(cxTop, t);
    auto emit = [&](ImVec2 p) {
        if (budget <= 0.0f) return;
        const float dx = p.x - prev.x, dy = p.y - prev.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-4f) {
            const float step = std::min(len, budget);
            dl->AddLine(prev, ImVec2(prev.x + dx / len * step, prev.y + dy / len * step), col, thickness);
            budget -= step;
        }
        prev = p;
    };
    constexpr int kCornerSteps = 8;
    emit(ImVec2(l + r + straightW, t));                       // 顶边
    for (int i = 1; i <= kCornerSteps; ++i) {                 // 右上圆角
        const float a = -1.5707963f + 1.5707963f * (float)i / kCornerSteps;
        emit(ImVec2(rr - r + std::cos(a) * r, t + r + std::sin(a) * r));
    }
    emit(ImVec2(rr, t + r + straightH));                      // 右边
    for (int i = 1; i <= kCornerSteps; ++i) {                 // 右下圆角
        const float a = 1.5707963f * (float)i / kCornerSteps;
        emit(ImVec2(rr - r + std::cos(a) * r, b - r + std::sin(a) * r));
    }
    emit(ImVec2(l + r, b));                                   // 底边
    for (int i = 1; i <= kCornerSteps; ++i) {                 // 左下圆角
        const float a = 1.5707963f + 1.5707963f * (float)i / kCornerSteps;
        emit(ImVec2(l + r + std::cos(a) * r, b - r + std::sin(a) * r));
    }
    emit(ImVec2(l, t + r));                                   // 左边
    for (int i = 1; i <= kCornerSteps; ++i) {                 // 左上圆角
        const float a = 3.14159265f + 1.5707963f * (float)i / kCornerSteps;
        emit(ImVec2(l + r + std::cos(a) * r, t + r + std::sin(a) * r));
    }
    emit(ImVec2(cxTop, t));                                   // 回到顶部中点
}

} // namespace

void StageHud::GetEnergyIconRect(float& cx, float& cy, float& size) {
    // 布局：窗口底部中央并排一行，能量组（图标+槽）在左、俯冲长条在右，整组水平居中；
    // 绘制与点击判定共用此几何
    size = HudIconSize;
    cx = (float)Config::GetInstance().GetScreenWidth() * 0.5f
         - HudBottomGroupW * 0.5f + size * 0.5f;
    cy = BottomRowCy();
}

void StageHud::DrawPrimary(const Snapshot& s, UIWidget& energyIcon) {
    DrawScore(s);
    DrawEnergy(s, energyIcon);
    DrawCoins(s);
    DrawDive(s);
}

void StageHud::DrawOverlay(const Snapshot& s) {
    DrawModeTimer(s);
    DrawCombo(s);
}

void StageHud::DrawScore(const Snapshot& s) {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 底板（左上角，紧邻帧率 overlay 下方）
    float hudW = HudScoreW;
    float hudH = HudScoreH;
    float hudX = HudLeftX + hudW * 0.5f;
    float hudY = ScorePanelCy();
    UIBuilder::DrawRoundedRect(
        hudX, hudY, hudW, hudH, 14.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::Amber, 0.9f), 2.0f);

    // 标签（左上）
    std::string label = "SCORE";
    float labelSize = 16.0f;
    dl->AddText(font, labelSize, ImVec2(hudX - hudW * 0.5f + 16.f + 1, hudY - hudH * 0.5f + 8.f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), label.c_str());
    dl->AddText(font, labelSize, ImVec2(hudX - hudW * 0.5f + 16.f, hudY - hudH * 0.5f + 8.f),
                UITheme::ToImU32(UITheme::Amber), label.c_str());

    // 分数（右侧垂直居中）
    std::string scoreStr = std::to_string(s.score);
    float scoreSize = 30.0f;
    ImVec2 sv = UIBuilder::CalcTextSize(scoreStr, scoreSize);
    float scoreX = hudX + hudW * 0.5f - 16.f - sv.x;
    float scoreY = hudY + 6.f - sv.y * 0.5f;
    dl->AddText(font, scoreSize, ImVec2(scoreX + 2, scoreY + 2),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f)), scoreStr.c_str());
    dl->AddText(font, scoreSize, ImVec2(scoreX, scoreY),
                UITheme::ToImU32(UITheme::Moonlight), scoreStr.c_str());
}

void StageHud::DrawEnergy(const Snapshot& s, UIWidget& energyIcon) {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // ===== 布局：窗口底部中央并排一行，能量组（图标+槽）在俯冲长条左侧 =====
    float iconSize, iconCx, rowY;
    GetEnergyIconRect(iconCx, rowY, iconSize);
    const float barX0 = iconCx + iconSize * 0.5f + HudBarGap; // 图标右缘 + 6
    const float barW = HudBarW;
    const float barH = 26.0f;
    const float barCx = barX0 + barW * 0.5f;

    const bool isFull = s.energy >= 1.0f;
    const bool isActive = s.shieldTimer > 0.0f;
    float pulse = 0.5f + 0.5f * sin(s.pulseClock * 6.0f); // 0~1 脉冲

    // ===== 图标（闪电） =====
    // 命中区与绘制共用同一份几何；仅能量充满时启用，未充满时点击自然透传给振翅
    energyIcon.SetRect(UIRect::FromCenter(iconCx, rowY, iconSize, iconSize));
    energyIcon.SetEnabled(isFull);
    const bool iconHovered = energyIcon.IsHovered();

    UIBuilder::DrawIconBackground(iconCx, rowY, iconSize, iconHovered, energyIcon.IsPressed());
    {
        // 闪电符号：上下两个三角形拼成
        ImU32 boltCol = isActive ? UITheme::ToImU32(UITheme::Moonlight)
                                  : UITheme::ToImU32(UITheme::Amber, 0.9f);
        if (isFull && !isActive) {
            // 充满时金色向白色脉冲
            ImU32 white = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f));
            boltCol = UITheme::LerpImU32(UITheme::ToImU32(UITheme::Amber), white, pulse * 0.7f);
        }
        ImU32 boltGlow = isActive ? UITheme::ToImU32(UITheme::Moonlight, 0.45f)
                                  : UITheme::ToImU32(UITheme::Amber, 0.45f);
        float w = iconSize * 0.28f, h = iconSize * 0.55f;
        // 上半段（斜向下的平行四边形近似为三角形）
        dl->AddTriangleFilled(
            ImVec2(iconCx - w * 0.5f + 1, rowY - h * 0.5f + 1),
            ImVec2(iconCx + w * 0.5f + 1, rowY - h * 0.5f + 1),
            ImVec2(iconCx - w * 0.1f + 1, rowY + 1), boltGlow);
        dl->AddTriangleFilled(
            ImVec2(iconCx - w * 0.5f, rowY - h * 0.5f),
            ImVec2(iconCx + w * 0.5f, rowY - h * 0.5f),
            ImVec2(iconCx - w * 0.1f, rowY), boltCol);
        // 下半段
        dl->AddTriangleFilled(
            ImVec2(iconCx + w * 0.5f + 1, rowY + h * 0.5f + 1),
            ImVec2(iconCx - w * 0.5f + 1, rowY + h * 0.5f + 1),
            ImVec2(iconCx + w * 0.1f + 1, rowY + 1), boltGlow);
        dl->AddTriangleFilled(
            ImVec2(iconCx + w * 0.5f, rowY + h * 0.5f),
            ImVec2(iconCx - w * 0.5f, rowY + h * 0.5f),
            ImVec2(iconCx + w * 0.1f, rowY), boltCol);

        // 护盾生效中：图标外圈发光描边
        if (isActive) {
            dl->AddRect(ImVec2(iconCx - iconSize * 0.5f - 3, rowY - iconSize * 0.5f - 3),
                        ImVec2(iconCx + iconSize * 0.5f + 3, rowY + iconSize * 0.5f + 3),
                        UITheme::ToImU32(UITheme::Moonlight, 0.5f + 0.4f * pulse),
                        iconSize * 0.3f, 0, 2.5f);
        }
    }

    // ===== 能量槽底板 =====
    UIBuilder::DrawRoundedRect(barCx, rowY, barW, barH, barH * 0.5f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(isActive ? UITheme::Moonlight : UITheme::AmberDim, 0.9f), 2.0f);

    // ===== 填充 =====
    const float pad = 4.0f;
    float frac = isActive ? s.shieldTimer / s.shieldDuration : s.energy;
    frac = std::clamp(frac, 0.0f, 1.0f);
    if (frac > 0.001f) {
        float fillW = (barW - pad * 2.0f) * frac;
        if (fillW > barH - pad * 2.0f) { // 保证小宽度下仍是胶囊形
            UIBuilder::DrawRoundedRect(barX0 + pad + fillW * 0.5f, rowY, fillW, barH - pad * 2.0f,
                (barH - pad * 2.0f) * 0.5f,
                UITheme::ToImU32(isActive ? UITheme::Moonlight : UITheme::Amber,
                                 isActive ? 0.9f : 1.0f),
                0, 0.0f);
        }
    }

    // ===== 状态提示文字 =====
    if (isActive) {
        UIBuilder::DrawCenteredText("无敌中", barCx, rowY, 18.0f,
            ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.1f, 0.9f)));
    } else if (isFull) {
        // 充满时在槽内闪烁提示（白字 <-> 暗字）
        ImU32 c = pulse > 0.5f ? ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f))
                               : ImGui::GetColorU32(ImVec4(0.15f, 0.1f, 0.02f, 0.9f));
        UIBuilder::DrawCenteredText("按空格释放", barCx, rowY, 18.0f, c);
    }
}

void StageHud::DrawCoins(const Snapshot& s) {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 布局：帧率 overlay 右缘 + 2px、与其垂直居中
    const float w = HudCoinsW;
    const float h = HudCoinsH;
    const float cx = FpsBoxRightX() + 2.0f + w * 0.5f;
    const float rowY = 10.0f + (FpsBoxBottomY() - 10.0f) * 0.5f;
    const float x0 = cx - w * 0.5f;

    UIBuilder::DrawRoundedRect(cx, rowY, w, h, h * 0.5f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // 金币图标：双层金色圆
    const float icx = x0 + 20.0f;
    dl->AddCircleFilled(ImVec2(icx + 1, rowY + 1), 9.0f,
                        UITheme::ToImU32(UITheme::AmberDim, 0.6f), 16);
    dl->AddCircleFilled(ImVec2(icx, rowY), 9.0f, UITheme::ToImU32(UITheme::Amber), 16);
    dl->AddCircle(ImVec2(icx, rowY), 5.5f, UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 12, 1.5f);

    // 数量（左对齐，金色）
    std::string coinStr = std::to_string(s.coins);
    dl->AddText(font, 18.0f, ImVec2(icx + 18.0f + 1, rowY - 9.0f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), coinStr.c_str());
    dl->AddText(font, 18.0f, ImVec2(icx + 18.0f, rowY - 9.0f),
                UITheme::ToImU32(UITheme::Amber), coinStr.c_str());
}

void StageHud::DrawDive(const Snapshot& s) {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 长条底板：窗口底部中央并排一行，位于能量组右侧（整组水平居中）
    const float w = DiveBarW;
    const float h = DiveBarH;
    const float cx = (float)Config::GetInstance().GetScreenWidth() * 0.5f
                     + HudBottomGroupW * 0.5f - w * 0.5f;
    const float cy = BottomRowCy();

    const bool ready = s.diveCooldown <= 0.0f && !s.diving;
    const bool diving = s.diving;
    float pulse = 0.5f + 0.5f * sin(s.pulseClock * 6.0f);

    UIBuilder::DrawRoundedRect(cx, cy, w, h, 8.0f,
                               UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
                               ready ? UITheme::ToImU32(UITheme::Amber, 0.65f + 0.3f * pulse)
                                     : UITheme::ToImU32(UITheme::AmberDim, 0.45f),
                               1.5f);

    // 内容：向下三角（俯冲意象）+「俯冲」文字，作为一组在条内居中；
    // 配色：俯冲中冰蓝 / 就绪金 / 冷却置灰
    const float tri = 10.0f;
    ImU32 col = diving ? UITheme::ToImU32({0.57f, 0.85f, 0.95f})
                       : (ready ? UITheme::ToImU32(UITheme::Amber)
                                : UITheme::ToImU32(UITheme::AmberDim, 0.7f));
    const ImVec2 ts = UIBuilder::CalcTextSize("俯冲", 20.0f);
    const float groupW = tri * 2.0f + 10.0f + ts.x;
    const float triCx = cx - groupW * 0.5f + tri;
    dl->AddTriangleFilled(ImVec2(triCx - tri, cy - tri * 0.6f), ImVec2(triCx + tri, cy - tri * 0.6f),
                          ImVec2(triCx, cy + tri), col);
    UIBuilder::DrawCenteredText("俯冲",
                                cx - groupW * 0.5f + tri * 2.0f + 10.0f + ts.x * 0.5f,
                                cy, 20.0f, col);

    // 冷却进度：自长条顶部中点起沿整个长条周长顺时针描边（剩余比例），替代原圆弧转圈
    if (s.diveCooldown > 0.0f && s.diveCooldownTotal > 0.0f) {
        const float frac = s.diveCooldown / s.diveCooldownTotal;
        StrokeRoundedRectPerimeter(dl,
                                   ImVec2(cx - w * 0.5f - 2.0f, cy - h * 0.5f - 2.0f),
                                   ImVec2(cx + w * 0.5f + 2.0f, cy + h * 0.5f + 2.0f),
                                   10.0f, frac,
                                   UITheme::ToImU32({0.57f, 0.85f, 0.95f}, 0.8f), 2.5f);
    }
}

void StageHud::DrawModeTimer(const Snapshot& s) {
    if (!s.timed) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", std::max(0.0f, s.timeLeft));
    ImU32 col = (s.timeLeft <= 10.0f)
        ? UITheme::ToImU32(UITheme::Blood, 0.7f + 0.3f * (0.5f + 0.5f * sin(s.pulseClock * 10.0f)))
        : UITheme::ToImU32(UITheme::Amber);
    // 金币栏（y13~43）下方、枭王名号（y88 起）上方
    UIBuilder::DrawCenteredText(buf, WIN_WIDTH * 0.5f, 60.0f, 30.0f, col);
}

void StageHud::DrawCombo(const Snapshot& s) {
    if (s.comboCount < 2 || s.comboTimer <= 0.0f) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    float frac = s.comboTimer / s.comboWindow; // 剩余窗口比例，越少越透明
    ImU32 col = UITheme::ToImU32(UITheme::Amber, 0.35f + 0.65f * frac);
    // 非限时模式紧随金币栏（y60）；限时模式移至能量行下方（y140）避让倒计时（y 来自模式参数表）
    UIBuilder::DrawCenteredText("连击 x" + std::to_string(s.comboCount),
                                WIN_WIDTH * 0.5f, s.comboHudY, 26.0f, col);
}

void StageHud::DrawCountdown(const Snapshot& s) {
    if (!s.counting) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    // 数字弹跳入场：前 35% 时间从 1.35 倍缩到 1.0 并淡入
    float popT = std::clamp(s.countdownTimer / (s.countdownStep * 0.35f), 0.0f, 1.0f);
    float scale = 1.35f - 0.35f * popT;
    float alpha = 0.25f + 0.75f * popT;
    UIBuilder::DrawCenteredText(std::to_string(s.countdownValue),
                                WIN_WIDTH * 0.5f, 280.0f, 120.0f * scale,
                                UITheme::ToImU32(UITheme::Amber, alpha));
}
