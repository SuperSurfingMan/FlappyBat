#include "app/Services.h"
#include "scene/ScoreRankScene.h"
#include "sprite/ButtonSprite.h"
#include "sprite/Sprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"
#include "ui/FontManager.h"
#include "res/ResourceManager.h"
#include "core/Config.h"
#include "core/Input.h"
#include "core/Util.h"
#include "app/ScoreRank.h"
#include "audio/AudioSystem.h"
#include "gameplay/ModeRegistry.h"
#include "layer/ToastLayer.h"
#include "scene/SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

// =============================================================================
// 清空记录的二次确认弹窗（模态）：居中卡片 + 取消/确认两个热区。
//
// 打开即压模态栈（dialogScope），关掉即弹栈；清空动作本身仍走 ScoreRank，
// 本文件只负责"问一次"与"画出来"。
// =============================================================================
void ScoreRankScene::ConfirmDialogRect(float& cx, float& cy, float& w, float& h) const {
    cx = (float)svc_.Cfg().GetScreenWidth() * 0.5f;
    cy = (float)svc_.Cfg().GetScreenHeight() * 0.49f;
    w = 420.f;
    h = 310.f;
}

void ScoreRankScene::OpenClearConfirm() {
    if (confirmClearOpen) return;
    confirmClearOpen = true;
    confirmFade = 0.f;
    // 场景按钮隐去（绘制与交互一并关闭）；弹窗内控件就绪，模态压栈由 Update 统一完成
    backToReady->SetShown(false);
    syncAndLoad->SetShown(false);
    clearRecords->SetShown(false);
    confirmYes->SetShown(true);
    confirmNo->SetShown(true);
    scrimZone->SetEnabled(true);
    dialogBody->SetEnabled(true);

    dialogScope.SetEnabled(true);
}

void ScoreRankScene::CloseClearConfirm() {
    confirmClearOpen = false;
    confirmFade = 0.f;
    confirmYes->SetShown(false);
    confirmNo->SetShown(false);
    scrimZone->SetEnabled(false);
    dialogBody->SetEnabled(false);
    backToReady->SetShown(true);
    syncAndLoad->SetShown(true);
    clearRecords->SetShown(true);

    dialogScope.SetEnabled(false);
}

void ScoreRankScene::DrawClearConfirmDialog() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    float cx, cy, dw, dh;
    ConfirmDialogRect(cx, cy, dw, dh);
    const float top = cy - dh * 0.5f;
    const float f = confirmFade; // 淡入进度

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 热区与遮罩/面板几何同源：遮罩整屏（点任意处取消），面板本体吞掉面板内空白点击
    scrimZone->SetRect(UIRect{0.f, 0.f, WIN_WIDTH, WIN_HEIGHT});
    dialogBody->SetRect(UIRect::FromCenter(cx, cy, dw, dh));

    // 全屏半透明遮罩：压暗并隔离背景 UI
    dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(WIN_WIDTH, WIN_HEIGHT),
                      ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.66f * f)));

    // 弹窗面板：与榜单面板同款暗色圆角 + 暗金描边（面板与文字不参与淡入，
    // 与不透明绘制的弹窗按钮保持一致；淡入仅作用于遮罩）
    UIBuilder::DrawRoundedRect(cx, cy, dw, dh, 16.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.97f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // 标题 + 警示文案（确认/取消按钮由 Render 中的 DrawLayout 绘制）
    UIBuilder::DrawCenteredText("清空记录", cx, top + 50.f, 34.0f,
                                UITheme::ToImU32(UITheme::Amber));
    UIBuilder::DrawCenteredText("确定要清空该模式的所有历史记录吗？", cx, cy - 25.f, 26.0f,
                                UITheme::ToImU32(UITheme::Moonlight));
    UIBuilder::DrawCenteredText(std::string(ScoreRank::ModeName(viewMode)) + " · 共 " +
                                std::to_string(scoreRank.size()) + " 条记录 · 此操作不可恢复",
                                cx, cy + 22.f, 20.0f,
                                UITheme::ToImU32(UITheme::Blood));
}
