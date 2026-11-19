#include "app/Services.h"
#include "layer/MainSceneUILayer.h"
#include "sprite/ButtonSprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"
#include "res/ResourceManager.h"
#include "sprite/Sprite.h"
#include "core/Config.h"
#include "scene/SceneManager.h"
#include "app/Application.h"
#include "audio/AudioSystem.h"

namespace {
// UI 层叠约定：数值越大越靠上，命中解析优先（全局统一表见 src/ui/UISystem.h 顶部说明）
constexpr int kLayerHudIcons   = 10;  // 局内顶部图标排（非模态）
constexpr int kLayerPausePanel = 40;  // 暂停面板（模态：压栈期间图标排让位）
}

MainSceneUILayer::MainSceneUILayer(Services& services) : svc_(services) {
    // 声明暂停快捷键（ESC/P/R）：交给 UISystem 每帧采集后推送本图层
    svc_.UI().WatchKeys({GLFW_KEY_ESCAPE, GLFW_KEY_P, GLFW_KEY_R});

    InitAllButtons();

    // GL 绘制的全屏遮罩（半透明黑）
    auto whiteTex = svc_.Res().GetTexture2D("white");
    if (whiteTex) {
        scrimSprite = std::make_shared<Sprite>(whiteTex);
        scrimSprite->SetColor(0.031f, 0.024f, 0.067f, 0.62f);
        scrimSprite->SetScale(svc_.Cfg().GetScreenWidth() + 4.f,
                              svc_.Cfg().GetScreenHeight() + 4.f);
        scrimSprite->SetPosition(svc_.Cfg().GetScreenWidth() * 0.5f,
                                 svc_.Cfg().GetScreenHeight() * 0.5f);
    }

    // 暂停面板（暗夜材质，luma key 透明叠加）
    auto panelTex = svc_.Res().GetTexture2D("v2_ui_panel");
    if (panelTex) {
        panelSprite = std::make_shared<Sprite>(panelTex);
        panelSprite->SetLumaKey(true, 0.06f, 0.10f);
    }

    // PAUSED 标题艺术字
    auto pausedTex = svc_.Res().GetTexture2D("v2_ui_text_paused");
    if (pausedTex) {
        pausedTextSprite = std::make_shared<Sprite>(pausedTex);
        pausedTextSprite->SetLumaKey(true, 0.06f, 0.10f);
    }

    // 组与模态绑定后默认关闭；MainScene 按入场/暂停/结算阶段开启
    hudScope.Bind(svc_.UI(), hudGroupId, false);
    pauseScope.Bind(svc_.UI(), pauseGroupId, true);
    SetInputEnabled(false);
}

MainSceneUILayer::~MainSceneUILayer() {
    // 兼底注销订阅（正常由 OnDetach 注销）：防退出路径残留悬垂 sink
    svc_.UI().RemoveGameInputSink(this);
}

void MainSceneUILayer::OnAttach() {
    svc_.UI().AddGameInputSink(this);
    SetInputEnabled(false); // 进入场景先收起交互，由 MainScene 按阶段开启
}

void MainSceneUILayer::OnDetach() {
    svc_.UI().RemoveGameInputSink(this);
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

void MainSceneUILayer::OnInputEnabledChanged(bool) {
    SyncInputGroups();
}

void MainSceneUILayer::OnUpdate(float dt) {
    // ===== 键盘快捷键（均由 UISystem 推送的 gameInput 快照读取，不再直接询设备） =====
    // ESC / P：切换暂停（未暂停 <-> 暂停）
    if (gameInput.Pressed(GLFW_KEY_ESCAPE) || gameInput.Pressed(GLFW_KEY_P)) {
        isPausing = !isPausing;
        svc_.Audio().PlaySFX(
            SFXToFile(isPausing ? SFX::Pause : SFX::Resume), 1.0f);
    }
    // R：暂停面板中快捷重新开始（与点击"重新开始"按钮等价）
    if (isPausing && gameInput.Pressed(GLFW_KEY_R)) {
        isPausing = false;
        svc_.Audio().PlaySFX(SFXToFile(SFX::Click), 1.0f);
        if (onRestartCallback) onRestartCallback();
    }

    // 交互闸门：暂停时图标排整体让位（旧写法仍在更新 home/quit，存在误点缺陷，已修正）
    SyncInputGroups();

    if (isPausing) {
        resumeButton->Update(dt);
        restartButton->Update(dt);
        homeButtonInPanel->Update(dt);
    } else {
        pauseButton->Update(dt);
    }
    homeButton->Update(dt);
    quitButton->Update(dt);
}

// 组开关与模态归属的唯一写入点：业务态变了就同步给 InputScope
void MainSceneUILayer::SyncInputGroups() {
    const bool on = InputEnabled();
    hudScope.SetEnabled(on && !isPausing);
    pauseScope.SetEnabled(on && isPausing);
}

void MainSceneUILayer::OnDraw(SpriteRenderer& renderer) {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    if (isPausing) {
        // 1) 全屏暗色遮罩（GL 绘制，位于所有精灵之上、ImGui 之下）
        if (scrimSprite) renderer.Draw_UV(scrimSprite);

        // 2) 面板
        if (panelSprite) {
            panelSprite->SetScale(700.f, 900.f);
            panelSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
            renderer.Draw_UV(panelSprite);
        }

        // 3) PAUSED 标题
        if (pausedTextSprite) {
            pausedTextSprite->SetScale(430.f, 215.f);
            pausedTextSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 260.f);
            renderer.Draw_UV(pausedTextSprite);
        }

        // 4) 面板内按钮（ImGui 绘制在最上层）
        resumeButton->DrawLayout();
        restartButton->DrawLayout();
        homeButtonInPanel->DrawLayout();
    } else {
        // 顶部图标按钮排
        pauseButton->DrawLayout();
        homeButton->DrawLayout();
        quitButton->DrawLayout();
    }
}

void MainSceneUILayer::InitAllButtons() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    // 交互控件全部登记到 UISystem：命中判定与事件派发由系统集中完成，
    // 注册顺序即层叠顺序（同组内后注册者在上）
    UISystem& ui = svc_.UI();
    hudGroupId   = ui.CreateGroup(kLayerHudIcons);
    pauseGroupId = ui.CreateGroup(kLayerPausePanel);

    // ===== 顶部图标按钮排（右上角） =====
    const float margin = 22.0f;
    const float iconSize = UITheme::IconBtnSize;
    const float iconSpacing = 16.0f;

    float cx = WIN_WIDTH - margin - iconSize * 0.5f;
    float cy = margin + iconSize * 0.5f; // ImGui 坐标 y 向下，顶部按钮排

    pauseButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Pause, Vec2{cx, cy}, iconSize);
    pauseButton->SetOnClick([this](){ isPausing = true; });
    ui.Add(hudGroupId, pauseButton);

    cx -= iconSize + iconSpacing;
    homeButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Home, Vec2{cx, cy}, iconSize);
    homeButton->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneReady);
    });
    ui.Add(hudGroupId, homeButton);

    cx -= iconSize + iconSpacing;
    quitButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Quit, Vec2{cx, cy}, iconSize);
    quitButton->SetOnClick([this]() {
        svc_.App().RequestQuit();
    });
    ui.Add(hudGroupId, quitButton);

    // ===== 暂停面板按钮（纵向，面板下半部） =====
    const float btnY0 = WIN_HEIGHT * 0.5f + 60.f;   // 继续按钮
    const float gap = 26.f;

    resumeButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f, btnY0},
        Vec2{UITheme::MainBtnW, UITheme::MainBtnH},
        "继续游戏");
    resumeButton->SetOnClick([this](){ isPausing = false; });
    ui.Add(pauseGroupId, resumeButton);

    restartButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{WIN_WIDTH * 0.5f, btnY0 + UITheme::MainBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "重新开始");
    restartButton->SetOnClick([this](){
        isPausing = false;
        if (onRestartCallback) onRestartCallback();
    });
    ui.Add(pauseGroupId, restartButton);

    homeButtonInPanel = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{WIN_WIDTH * 0.5f, btnY0 + UITheme::MainBtnH * 0.5f + gap * 2 + UITheme::SecondaryBtnH * 1.5f},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "返回主菜单");
    homeButtonInPanel->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneReady);
    });
    ui.Add(pauseGroupId, homeButtonInPanel);
}

void MainSceneUILayer::Reset() {
    pauseButton->Reset();
    homeButton->Reset();
    quitButton->Reset();
    resumeButton->Reset();
    restartButton->Reset();
    homeButtonInPanel->Reset();
    isPausing = false;
    // 残留的悬停/按下视觉一并清掉（注册关系保留，控件仍属本图层）
    svc_.UI().ClearWidgetState(hudGroupId);
    svc_.UI().ClearWidgetState(pauseGroupId);
    SyncInputGroups();
}

bool MainSceneUILayer::GetPauseState() const {
    return isPausing;
}
