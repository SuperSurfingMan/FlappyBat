#include "app/Services.h"
#include "layer/MainSceneOverLayer.h"
#include "sprite/ButtonSprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"
#include "res/ResourceManager.h"
#include "sprite/Sprite.h"
#include "core/Config.h"
#include "scene/SceneManager.h"
#include "core/Math.h"
#include <iostream>

namespace {
// 结算面板：模态组，层叠高于局内 HUD（面板一弹出就独占交互）
constexpr int kLayerOverPanel = 50;
}

MainSceneOverLayer::MainSceneOverLayer(Services& services) : svc_(services) {
    InitSprites();

    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    // GL 全屏遮罩
    auto whiteTex = svc_.Res().GetTexture2D("white");
    if (whiteTex) {
        scrimSprite = std::make_shared<Sprite>(whiteTex);
        scrimSprite->SetColor(0.031f, 0.024f, 0.067f, 0.65f);
        scrimSprite->SetScale(WIN_WIDTH + 4.f, WIN_HEIGHT + 4.f);
        scrimSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
    }

    // 结算面板
    auto panelTex = svc_.Res().GetTexture2D("v2_ui_panel");
    if (panelTex) {
        panelSprite = std::make_shared<Sprite>(panelTex);
        panelSprite->SetLumaKey(true, 0.06f, 0.10f);
    }

    // GAME OVER 艺术字
    auto goTex = svc_.Res().GetTexture2D("v2_ui_text_gameover");
    if (goTex) {
        gameOverTextSprite = std::make_shared<Sprite>(goTex);
        gameOverTextSprite->SetLumaKey(true, 0.06f, 0.10f);
    }

    // 结算层默认关闭，ActivateLayer 时整体打开
    overScope.Bind(svc_.UI(), overGroupId, true);
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

void MainSceneOverLayer::ActivateLayer() {
    // 已离开主场景（DetachAll 后）时不得重新开启：否则死亡动画尾帧
    // 迟到的结算回调会在别的场景里弹出结算模态、抢走点击。
    if (!IsAttached()) return;
    SetUpdateEnabled(true);
    SetRenderEnabled(true);
    SetInputEnabled(true);
}

void MainSceneOverLayer::OnAttach() {
    // 每次进入场景都从“未结算”开始；结算完成由 ActivateLayer 打开
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

void MainSceneOverLayer::OnDetach() {
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

void MainSceneOverLayer::OnInputEnabledChanged(bool) {
    SyncInputGroups();
}

// 组开关 + 模态压栈：面板激活时才参与命中，离开主场景时自动失效
void MainSceneOverLayer::SyncInputGroups() {
    overScope.SetEnabled(InputEnabled());
}

void MainSceneOverLayer::OnUpdate(float dt) {
    if (shouldBlink) {
        gameOverImage->Update(dt);
    }
    if (blinkTimer.IsRunning()) {
        blinkTimer.Update(dt);
    }

    restartButton->Update(dt);
    backToReadyButton->Update(dt);
}

void MainSceneOverLayer::OnDraw(SpriteRenderer& renderer) {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    const float cx = WIN_WIDTH * 0.5f;

    // 1) 全屏暗色遮罩（GL）
    if (scrimSprite) renderer.Draw_UV(scrimSprite);

    // 2) 结算面板（720 x 920，中心 (270, 480)）
    if (panelSprite) {
        panelSprite->SetScale(720.f, 920.f);
        panelSprite->SetPosition(cx, WIN_HEIGHT * 0.5f);
        renderer.Draw_UV(panelSprite);
    }

    // 3) GAME OVER 标题（顶部）
    if (gameOverTextSprite) {
        gameOverTextSprite->SetScale(470.f, 235.f);
        gameOverTextSprite->SetPosition(cx, 800.f);
        renderer.Draw_UV(gameOverTextSprite);
    }

    // 4) 蝙蝠眨眼动画（中部装饰）
    if (gameOverImage) {
        gameOverImage->SetScale(300.f, 300.f);
        gameOverImage->SetPosition(cx, 580.f);
        renderer.Draw_UV(gameOverImage);
    }

    // 5) 分数文本（ImGui 居中）
    DrawImGUILayout();

    // 6) 按钮（底部纵向）
    restartButton->DrawLayout();
    backToReadyButton->DrawLayout();
}

void MainSceneOverLayer::Reset() {
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
    if (gameOverImage) gameOverImage->Reset();
    if (restartButton) restartButton->Reset();
    if (backToReadyButton) backToReadyButton->Reset();
    blinkTimer.Reset();
    shouldBlink = true;
    blinkInterval = 2.0f;
    currentScore = 0;
    bestScore = 0;
    svc_.UI().ClearWidgetState(overGroupId);
    SyncInputGroups();
}

void MainSceneOverLayer::InitSprites() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();

    UISystem& ui = svc_.UI();
    overGroupId = ui.CreateGroup(kLayerOverPanel);

    // 蝙蝠眨眼动画
    gameOverImage = std::make_shared<Sprite>();
    gameOverImage->InitEmptyAnimation();
    const auto& anim = gameOverImage->GetAnimation();
    anim->SetLoop(false);
    anim->SetEmptyWhenDone(false);
    for (size_t i = 1; i <= 7; ++i) {
        std::string key = "batBlink0" + std::to_string(i);
        auto tex = svc_.Res().GetTexture2D(key);
        if (tex) anim->AddFrame(tex);
    }
    anim->MakeFrameUVSyncWithFrameList01();
    anim->SetFrameTime(0.02f);
    anim->setOnAnimationFinished([this](){
        this->shouldBlink = false;
        float interval = Random::Get().GetRandomFloat() * 3.f + 2.f;
        this->blinkInterval = interval;
        blinkTimer.Reset();
        blinkTimer.Start(interval, [this](){
            this->shouldBlink = true;
            this->ResetAnimState();
        });
    });

    // 按钮（面板下半部，纵向；ImGui 坐标 y 向下）
    const float btnY0 = 700.f; // 重新开始（主按钮）
    const float gap = 24.f;

    restartButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f, btnY0},
        Vec2{UITheme::MainBtnW, UITheme::MainBtnH},
        "重新开始");
    restartButton->SetOnClick([this](){
        if (onRestart) onRestart();
    });
    ui.Add(overGroupId, restartButton);

    backToReadyButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{WIN_WIDTH * 0.5f, btnY0 + UITheme::MainBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "返回主菜单");
    backToReadyButton->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneReady);
    });
    ui.Add(overGroupId, backToReadyButton);
}

void MainSceneOverLayer::DrawImGUILayout() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();

    const float offset = 220.f;

    // 本局分数（蝙蝠下方）
    std::string scoreText = "本局分数  " + std::to_string(currentScore);
    UIBuilder::DrawCenteredText(scoreText, WIN_WIDTH * 0.5f, 390.f + offset, 30.0f,
                                UITheme::ToImU32(UITheme::Moonlight));

    // 最高分
    std::string bestText = "最高分  " + std::to_string(bestScore);
    UIBuilder::DrawCenteredText(bestText, WIN_WIDTH * 0.5f, 352.f + offset, 24.0f,
                                UITheme::ToImU32(UITheme::Amber));
}

void MainSceneOverLayer::ResetAnimState() {
    if (!gameOverImage) return;
    const auto& anim = gameOverImage->GetAnimation();
    anim->Reset();
}
