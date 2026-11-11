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

namespace {
// UI 层叠：榜单主控件组与确认弹窗（模态）
constexpr int kLayerRankMain = 20;
constexpr int kLayerConfirmDialog = 60;
}

// =============================================================================
// 排行榜场景：数据加载、切页与生命周期。
//
// 第 5 期拆分后本文件只留"场景本身"：
//   ScoreRankSceneView.cpp   页签行 / 榜单列表 / 滚动条与标题文字绘制
//   ScoreRankSceneDialog.cpp 清空记录的二次确认弹窗（模态）
// =============================================================================

ScoreRankScene::ScoreRankScene(Services& services) : svc_(services) {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    // UI 控件组：榜单主控件（非模态）+ 确认弹窗（模态）
    // 注册顺序即层叠顺序：后注册者在上（页签/滑块 > 滚轮区/滑轨 > 遮罩 > 按钮）
    UISystem& ui = svc_.UI();
    rankGroupId   = ui.CreateGroup(kLayerRankMain);
    dialogGroupId = ui.CreateGroup(kLayerConfirmDialog);
    rankScope.Bind(ui, rankGroupId, false);
    dialogScope.Bind(ui, dialogGroupId, true);

    // 背景：钟楼内景（优先 v2，失败回退旧背景）
    auto bgTex = svc_.Res().GetTexture2D("v2_ui_scoreboard_bg");
    if (!bgTex) bgTex = svc_.Res().GetTexture2D("scoreRankBackground");
    background = std::make_shared<Sprite>(bgTex);
    background->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
    background->SetScale(WIN_WIDTH, WIN_HEIGHT);

    // 标题艺术字 HIGH SCORES
    if (auto tex = svc_.Res().GetTexture2D("v2_ui_text_rank")) {
        titleSprite = std::make_shared<Sprite>(tex);
        titleSprite->SetLumaKey(true, 0.06f, 0.12f);
        titleSprite->SetScale(460.f, 230.f);
        titleSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT - 170.f); // GL 坐标 y 向上，标题置顶
    }

    // 底部双按钮并排：返回主菜单（主按钮）+ 清空记录（危险按钮）（ImGui 坐标 y 向下）
    backToReady = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f - 106.f, WIN_HEIGHT - 90.f},
        Vec2{290.f, UITheme::MainBtnH},
        "返回主菜单");
    backToReady->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneReady);
    });
    ui.Add(rankGroupId, backToReady);

    clearRecords = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Danger,
        Vec2{WIN_WIDTH * 0.5f + 158.f, WIN_HEIGHT - 90.f},
        Vec2{186.f, UITheme::MainBtnH},
        "清空记录");
    clearRecords->SetOnClick([this](){
        if (scoreRank.empty()) {
            toastLayer->SetText("暂无记录可清空");
            toastLayer->SetColor(Vec3{0.941f, 0.722f, 0.314f});
            toastLayer->Activate();
            return;
        }
        OpenClearConfirm();
    });
    ui.Add(rankGroupId, clearRecords);

    // 右上角小图标：重新加载（ImGui 坐标 y 向下）
    syncAndLoad = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Restart,
        Vec2{WIN_WIDTH - 52.f, 52.f},
        52.f);
    syncAndLoad->SetOnClick([this](){
        scoreRank = svc_.Scores().GetScoresFromDisk(viewMode);
        toastLayer->SetText("已加载最新记录");
        toastLayer->SetColor(Vec3{0.4f, 0.85f, 0.4f});
        toastLayer->Activate();
    });
    ui.Add(rankGroupId, syncAndLoad);

    // Toast（顶部居中）
    Vec2 toastPosition = {WIN_WIDTH * 0.5f, 60.f};
    toastLayer = std::make_shared<ToastLayer>(2.0f, "已加载最新记录", Vec3{0.4f, 0.85f, 0.4f}, 24.0f, toastPosition);

    // 确认弹窗内的两个按钮（仅在弹窗打开时激活）
    float dialogCx, dialogCy, dialogW, dialogH;
    ConfirmDialogRect(dialogCx, dialogCy, dialogW, dialogH);

    confirmNo = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{dialogCx - 85.f, dialogCy + 86.f},
        Vec2{160.f, 72.f},
        "取消");
    confirmNo->SetOnClick([this](){
        CloseClearConfirm();
    });

    confirmYes = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Danger,
        Vec2{dialogCx + 85.f, dialogCy + 86.f},
        Vec2{160.f, 72.f},
        "确认清空");
    confirmYes->SetOnClick([this](){
        // 一次性清空当前模式内存榜单并同步写入本地 score.dat，随后重新读盘确认结果
        svc_.Scores().ClearScores(viewMode);
        scoreRank = svc_.Scores().GetScoresFromDisk(viewMode);
        listScroll = listScrollTarget = 0.f;
        CloseClearConfirm();
        toastLayer->SetText(std::string("已清空") + ScoreRank::ModeName(viewMode) + "记录");
        toastLayer->SetColor(Vec3{0.870f, 0.360f, 0.360f});
        toastLayer->Activate();
    });

    // ===== 内联热区控件登记（矩形与几何在绘制函数中与绘制同源同步） =====
    // 注册顺序决定层叠：先滚轮区（最大面积、最下层），再滑轨，最后滑块（优先被抓取）
    listZone->SetPlayFeedbackSFX(false);     // 纯滚轮热区：不发悬停/点击声
    ui.Add(rankGroupId, listZone);

    scrollTrack->SetPlayFeedbackSFX(false);
    ui.Add(rankGroupId, scrollTrack);

    scrollThumb->SetPlayFeedbackSFX(false);
    scrollThumb->SetCaptureOnPress(true);    // 按下即捕获：拖拽移出滑轨仍连续跟随
    ui.Add(rankGroupId, scrollThumb);

    tabs.resize(ModeRegistry::Instance().Size()); // 页签数量由注册表动态决定
    for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i) {
        tabs[i] = std::make_shared<UIWidget>();
        tabs[i]->SetOnClick([this, i]() { SwitchViewMode(ModeRegistry::Instance().At((size_t)i).Key()); });
        ui.Add(rankGroupId, tabs[i]);        // 页签最后注册：盖在滚轮区之上
    }

    // 弹窗层叠（后注册者在上）：遮罩 > 面板本体（吞掉面板内空白点击）> 取消/确认按钮
    scrimZone->SetPlayFeedbackSFX(false);
    scrimZone->SetOnClick([this]() { CloseClearConfirm(); });
    ui.Add(dialogGroupId, scrimZone);

    dialogBody->SetPlayFeedbackSFX(false);
    ui.Add(dialogGroupId, dialogBody);

    ui.Add(dialogGroupId, confirmNo);
    ui.Add(dialogGroupId, confirmYes);

    confirmNo->SetShown(false);
    confirmYes->SetShown(false);
    scrimZone->SetEnabled(false);
    tabs[0]->SetEnabled(false); // 选中页签不可点，后续由 DrawModeTabs 逐帧刷新
}

ScoreRankScene::~ScoreRankScene() {}

void ScoreRankScene::OnEnter() {
    viewMode = kDefaultModeKey; // 每次进入默认查看经典模式页签
    LoadRankList();
    // 每次进入榜单回到顶部
    listScroll = listScrollTarget = 0.f;

    rankScope.SetEnabled(true);
    dialogScope.SetEnabled(false);
}

void ScoreRankScene::OnExit() {
    CloseClearConfirm();
    scoreRank.clear();
    rankScope.SetEnabled(false);
    dialogScope.SetEnabled(false);
}

void ScoreRankScene::Update(float dt) {
    background->Update(dt);

    // 弹窗开合：组开关与模态压栈由 InputScope 原子同步
    dialogScope.SetEnabled(confirmClearOpen);

    if (confirmClearOpen) {
        // 弹窗打开：仅淡入推进与弹窗按钮动画；榜单滚动在绘制侧因控件禁用而自然挂起
        confirmFade = std::min(1.f, confirmFade + dt * 6.f);
    }

    // 按钮动画统一推进（Sprite::Update 对未激活者自动 no-op）
    backToReady->Update(dt);
    syncAndLoad->Update(dt);
    clearRecords->Update(dt);
    confirmYes->Update(dt);
    confirmNo->Update(dt);

    toastLayer->Tick(dt);
}

void ScoreRankScene::Render(SpriteRenderer& renderer) {
    renderer.Draw_UV(background);
    if (titleSprite) renderer.Draw_UV(titleSprite);

    DrawImGuiRank();

    backToReady->DrawLayout();
    syncAndLoad->DrawLayout();
    clearRecords->DrawLayout();

    toastLayer->Draw(renderer);

    // 确认弹窗最后绘制，覆盖在场景 UI 之上
    if (confirmClearOpen) {
        DrawClearConfirmDialog();
        confirmNo->DrawLayout();
        confirmYes->DrawLayout();
    }
}

void ScoreRankScene::Reset() {
    CloseClearConfirm();
    backToReady->Reset();
    background->Reset();
    syncAndLoad->Reset();
    clearRecords->Reset();
    confirmYes->Reset();
    confirmNo->Reset();
    toastLayer->Reset();
    scoreRank.clear();
    viewMode = kDefaultModeKey;
    listScroll = listScrollTarget = 0.f;
    svc_.UI().ClearWidgetState(rankGroupId);
    svc_.UI().ClearWidgetState(dialogGroupId);
}

void ScoreRankScene::LoadRankList() {
    scoreRank = svc_.Scores().GetScores(viewMode);
}

void ScoreRankScene::SwitchViewMode(ModeKey mode) {
    if (mode == viewMode) return;
    viewMode = mode;
    scoreRank = svc_.Scores().GetScores(viewMode);
    listScroll = listScrollTarget = 0.f; // 切换模式回到顶部
}

// ===== 模式页签行：三个模式独立榜单切换（当前模式高亮；弹窗打开时由模态组自动让位） =====
