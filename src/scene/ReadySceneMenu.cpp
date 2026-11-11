#include "app/Services.h"
#include "scene/ReadyScene.h"
#include "scene/ReadySceneLayout.h"
#include "scene/SceneManager.h"
#include "core/Config.h"
#include "app/Application.h"
#include "app/PlayerProfile.h"
#include "res/ResourceManager.h"
#include "audio/AudioSystem.h"
#include "sprite/BatSprite.h"
#include "sprite/ButtonSprite.h"
#include "gameplay/ModeRegistry.h"
#include "gameplay/StageParams.h"
#include "ui/UIBuilder.h"
#include "ui/UISystem.h"
#include "ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace {

// 布局常量集中一处（ReadySceneLayout.h）；模式数量始终从注册表推导
using namespace ready::layout;

float ModeOffset(int i) {
    return ModeOffsetX(i, ModeRegistry::Instance().Size());
}

} // namespace

// =============================================================================
// 主菜单的"开始游戏"分裂式模式选择（B9 改版）：
//   悬停 0.2s -> 主按钮横向分裂为 N 个模式按钮（自中心滑出 + 展宽 + 淡入）；
//   光标停留在整行横向区域维持分裂，离开后平滑收缩；
//   悬停单个模式按钮 0.5s -> 光标旁淡入解释框；点击后经 StartGameWithMode 启动转场。
//
// 本文件只读控件悬停事实（UISystem 是唯一命中判定者），不自己判定鼠标。
// =============================================================================

void ReadyScene::UpdateModeSplit(float dt) {
    // 转场期间：冻结当前分裂状态（点击模式按钮后不回缩），不响应悬停/点击
    if (svc_.Scenes().IsTransitionActive()) {
        modeHoverIndex = -1;
        modeHoverTimer = 0.0f;
        tipFade = std::max(0.0f, tipFade - dt * 10.0f);
        if (tipFade <= 0.0f) tipIndex = -1;
        return;
    }

    // 热区可交互性：分裂行区域只在已有分裂进度时接管（合并态让位给"开始游戏"本体，
    // 保持"必须先悬停本体才分裂"的门槛）；模式按钮只在完全分裂后参与
    startRowZone->SetEnabled(!upgradePanelOpen && modeSplitProgress > 0.001f);
    for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i)
        modeZones[i]->SetEnabled(!upgradePanelOpen && modeSplitProgress >= 0.999f);

    // 升级面板为模态覆盖层：强制收起并跳过交互
    if (upgradePanelOpen) {
        splitOpen = false;
        rowHoverTimer = 0.0f;
        modeHoverIndex = -1;
        modeHoverTimer = 0.0f;
        tipFade = std::max(0.0f, tipFade - dt * 10.0f);
        if (tipFade <= 0.0f) tipIndex = -1;
    } else {
        // ===== 悬停事实全部来自控件：本帧唯一被 UISystem 判为悬停的控件才是激活者 =====
        int hoverIdx = -1;
        for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i) {
            if (modeZones[i]->IsHovered()) { hoverIdx = i; break; }
        }

        // 持续分裂判定区 = 整行热区 / 任意模式按钮 / 开始按钮本体。
        // 必须包含 mainHot：热区与按钮之间的悬停归属交接有一帧延迟
        //（SetEnabled 在 Update 中生效，而命中判定已在本帧更早的 Dispatch 完成），
        // 交接当帧两者可能同时为空，若只看热区会把刚触发的分裂立即误判为“离开”而收缩。
        const bool mainHot = !menuButtons.empty() && menuButtons[0]->IsHovered();
        const bool rowHot  = startRowZone->IsHovered() || hoverIdx >= 0 || mainHot;

        // 悬停计时：未分裂时需悬停在"开始游戏"按钮本体上；分裂后整个横向区域均可维持
        if (splitOpen ? rowHot : mainHot) {
            rowHoverTimer += dt;
        } else {
            rowHoverTimer = 0.0f;
        }

        // 悬停满阈值 -> 分裂；离开横向区域 -> 收缩
        if (rowHoverTimer >= kSplitHoverDelay) splitOpen = true;
        if (modeSplitProgress > 0.001f && !rowHot) splitOpen = false;

        // 连续悬停同一模式按钮才累计（换按钮/离开则重新计时）
        if (hoverIdx >= 0) {
            if (hoverIdx == modeHoverIndex) {
                modeHoverTimer += dt;
            } else {
                modeHoverIndex = hoverIdx;
                modeHoverTimer = 0.0f;
            }
        } else {
            modeHoverIndex = -1;
            modeHoverTimer = 0.0f;
        }
        // 模式按钮的点击经各自 onClick 回调直达 StartGameWithMode（点击音效由 UISystem 播放）
    }

    // ===== 解释框：悬停满 0.5s 淡入，离开（换按钮/离区）立即淡出 =====
    if (modeHoverIndex >= 0 && modeHoverTimer >= kTipHoverDelay) {
        tipIndex = modeHoverIndex;
        tipFade = std::min(1.0f, tipFade + dt * 8.0f);
    } else {
        tipFade = std::max(0.0f, tipFade - dt * 10.0f);
        if (tipFade <= 0.0f) tipIndex = -1;
    }

    // ===== 分裂/收缩进度动画（线性推进，渲染时施加 smoothstep 缓动） =====
    const float speed = splitOpen ? (1.0f / 0.22f) : (1.0f / 0.30f);
    if (splitOpen) {
        modeSplitProgress = std::min(1.0f, modeSplitProgress + speed * dt);
    } else {
        modeSplitProgress = std::max(0.0f, modeSplitProgress - speed * dt);
    }
}

// 带整体透明度的主按钮风格底板（颜色/圆角/描边与 UIBuilder::DrawButton 主按钮一致）

void ReadyScene::DrawSplitButtonPlate(float cx, float cy, float w, float h, float alpha,
                                      bool hovered, const char* text, float fontSize,
                                      float textAlpha) {
    if (alpha <= 0.01f) return;

    UIBuilder::DrawShadow(cx, cy + h * 0.4f, w * 0.95f, h * 0.4f, 0.45f * alpha);
    UIBuilder::DrawButtonPlate(cx, cy, w, h, UITheme::CornerRadius,
        UITheme::ToImU32({0.243f, 0.169f, 0.396f}, alpha),
        UITheme::ToImU32({0.180f, 0.114f, 0.310f}, alpha),
        UITheme::ToImU32(hovered ? UITheme::Amber : UITheme::AmberDim, alpha),
        hovered ? 4.0f : UITheme::BorderWidth);

    if (!text || textAlpha <= 0.01f) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 textCol = UITheme::ToImU32(hovered ? UITheme::Amber : UITheme::Moonlight, textAlpha);
    ImVec2 ts = UIBuilder::CalcTextSize(text, fontSize);
    dl->AddText(font, fontSize,
                ImVec2(cx - ts.x * 0.5f + 2.0f, cy - ts.y * 0.5f + 2.0f),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f * textAlpha)), text);
    dl->AddText(font, fontSize,
                ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), textCol, text);
}

void ReadyScene::DrawStartButtonRow() {
    if (modeSplitProgress <= 0.001f) return; // 完全合并：常规开始按钮由 ButtonSprite 绘制

    const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();
    const float cx = WIN_WIDTH * 0.5f;

    const float p = modeSplitProgress;
    const float e = p * p * (3.0f - 2.0f * p); // smoothstep：位置/尺寸插值缓动

    // ===== 主按钮：淡出并轻微收缩，为三个模式按钮让位 =====
    const float mainAlpha = 1.0f - std::clamp(p / 0.55f, 0.0f, 1.0f);
    if (mainAlpha > 0.01f) {
        const float mw = UITheme::MainBtnW * (1.0f - 0.45f * e);
        DrawSplitButtonPlate(cx, kStartBtnY, mw, UITheme::MainBtnH, mainAlpha,
                             false, "开始游戏", UITheme::FontButton, mainAlpha);
    }

    // ===== 模式按钮：自中心滑出 + 展宽 + 淡入 =====
    const float subAlpha = std::clamp((p - 0.12f) / 0.55f, 0.0f, 1.0f);
    if (subAlpha > 0.01f) {
        for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i) {
            const float fx = cx + ModeOffset(i) * (kModeSubBtnW + kModeSubBtnGap); // 最终中心
            const float bx = cx + (fx - cx) * e;                                  // 自中心滑出
            const float bw = 46.0f + (kModeSubBtnW - 46.0f) * e;                   // 逐渐展宽
            // 文字随按钮展宽浮现（过窄时先不显示，避免溢出）
            const float textAlpha = subAlpha *
                std::clamp((bw - 70.0f) / (kModeSubBtnW - 70.0f), 0.0f, 1.0f);
            DrawSplitButtonPlate(bx, kStartBtnY, bw, UITheme::MainBtnH, subAlpha,
                                 modeHoverIndex == i,
                                 ModeRegistry::Instance().At((size_t)i).DisplayName(), 20.0f, textAlpha);
        }
    }
}

// 模式解释小框：悬停模式按钮 0.5s 后在光标旁淡入，离开即淡出

void ReadyScene::DrawModeTipBox() {
    if (tipFade <= 0.01f || tipIndex < 0) return;

    if ((size_t)tipIndex >= ModeRegistry::Instance().Size()) return; // 越界不绘制，绝不读非法下标

    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    const float fontSize = 15.0f;
    // 文案统一取自模式参数表，消除第二份并行数组
    const std::string text = ModeRegistry::Instance().At((size_t)tipIndex).MenuDesc();
    ImVec2 ts = UIBuilder::CalcTextSize(text, fontSize);
    const float padX = 12.0f, padY = 9.0f;
    const float w = ts.x + padX * 2.0f;
    const float h = ts.y + padY * 2.0f;

    // 默认位于光标右侧并垂直居中；越过右边界时翻到左侧
    // 光标位置取自 UISystem（本帧快照）：控件侧不再直接读 Input
    const Vec2 ptr = svc_.UI().GetPointer();
    const float mx = ptr.x;
    const float my = ptr.y;
    float x0 = mx + 16.0f;
    if (x0 + w > WIN_WIDTH - 8.0f) x0 = mx - 16.0f - w;
    float y0 = std::clamp(my - h * 0.5f, 8.0f, WIN_HEIGHT - h - 8.0f);

    const float a = tipFade;
    // 极小解释框：暗底 + 暗金细描边 + 模式说明
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h),
                      ImGui::GetColorU32(ImVec4(0.06f, 0.05f, 0.11f, 0.96f * a)), 8.0f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h),
                UITheme::ToImU32(UITheme::AmberDim, 0.9f * a), 8.0f, 0, 1.5f);
    dl->AddText(font, fontSize, ImVec2(x0 + padX + 1.0f, y0 + padY + 1.0f),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * a)), text.c_str());
    dl->AddText(font, fontSize, ImVec2(x0 + padX, y0 + padY),
                UITheme::ToImU32(UITheme::Moonlight, 0.95f * a), text.c_str());
}

void ReadyScene::StartGameWithMode(int modeIndex) {
    // 下标 -> 枚举的隐式契约显式校验：漏放热区或下标错位会立即暴露，
    // 而不是静默越界读到别的模式参数。
    assert(modeIndex >= 0 && (size_t)modeIndex < ModeRegistry::Instance().Size());
    if (modeIndex < 0 || (size_t)modeIndex >= ModeRegistry::Instance().Size()) return;

    auto& sm = svc_.Scenes();
    if (sm.IsTransitionActive()) return; // 转场进行中不重复启动

    // 保持分裂展开状态（转场期间冻结、不回缩；返回主菜单时由 Reset 复位为单按钮），
    // 仅清除悬停高亮与解释框
    modeHoverIndex = -1;
    modeHoverTimer = 0.0f;
    tipFade = 0.0f;
    tipIndex = -1;

    // 一行式意图：目标场景 + 对局参数 + 转场样式；路由、参数注入与转场编排全部在 SceneManager
    SceneArgs args;
    args.gameMode = ModeRegistry::Instance().At((size_t)modeIndex).Key();
    std::cout << "Mode selected: " << modeIndex << ", starting transition..." << std::endl;

    // 以蝙蝠当前位置为圆心聚焦（主界面蝙蝠同位同尺寸，营造"同一只蝙蝠"的连续感）；
    // 精灵世界坐标 Y 向上，转场锚点用屏幕坐标（Y 向下），此处换算后随请求携带
    const auto& p = batSprite->GetPosition();
    args.focusAnchorScreen = Vec2{ p.x, (float)svc_.Cfg().GetScreenHeight() - p.y };
    sm.RequestScene(kSceneMain, args, kTransitionCircleFocus);
}

// ===== 护盾升级（B8）：右上角金币入口 + 升级面板 =====
