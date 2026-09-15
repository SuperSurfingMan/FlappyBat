#include "MainSceneUILayer.h"
#include "../sprite/ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../res/ResourceManager.h"
#include "../sprite/Sprite.h"
#include "../Config.h"
#include "../scene/SceneManager.h"
#include "../app/Application.h"
#include "../Input.h"
#include "../audio/AudioSystem.h"

MainSceneUILayer::MainSceneUILayer() {
    InitAllButtons();

    // GL 绘制的全屏遮罩（半透明黑）
    auto whiteTex = ResourceManager::Get().GetTexture2D("white");
    if (whiteTex) {
        scrimSprite = std::make_shared<Sprite>(whiteTex);
        scrimSprite->SetColor(0.031f, 0.024f, 0.067f, 0.62f);
        scrimSprite->SetScale(Config::GetInstance().GetScreenWidth() + 4.f,
                              Config::GetInstance().GetScreenHeight() + 4.f);
        scrimSprite->SetPosition(Config::GetInstance().GetScreenWidth() * 0.5f,
                                 Config::GetInstance().GetScreenHeight() * 0.5f);
    }

    // 暂停面板（暗夜材质，luma key 透明叠加）
    auto panelTex = ResourceManager::Get().GetTexture2D("v2_ui_panel");
    if (panelTex) {
        panelSprite = std::make_shared<Sprite>(panelTex);
        panelSprite->SetLumaKey(true, 0.06f, 0.10f);
    }

    // PAUSED 标题艺术字
    auto pausedTex = ResourceManager::Get().GetTexture2D("v2_ui_text_paused");
    if (pausedTex) {
        pausedTextSprite = std::make_shared<Sprite>(pausedTex);
        pausedTextSprite->SetLumaKey(true, 0.06f, 0.10f);
    }
}

void MainSceneUILayer::Update(float dt) {
    // ===== 键盘快捷键 =====
    // ESC / P：切换暂停（未暂停 <-> 暂停）
    if (Input::IsKeyPressed(GLFW_KEY_ESCAPE) || Input::IsKeyPressed(GLFW_KEY_P)) {
        isPausing = !isPausing;
        AudioSystem::Instance().PlaySFX(
            SFXToFile(isPausing ? SFX::Pause : SFX::Resume), 0.9f);
    }
    // R：暂停面板中快捷重新开始（与点击"重新开始"按钮等价）
    if (isPausing && Input::IsKeyPressed(GLFW_KEY_R)) {
        isPausing = false;
        AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Click), 1.0f);
        if (onRestartCallback) onRestartCallback();
    }

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

void MainSceneUILayer::Render(SpriteRenderer& renderer) {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

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
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // ===== 顶部图标按钮排（右上角） =====
    const float margin = 22.0f;
    const float iconSize = UITheme::IconBtnSize;
    const float iconSpacing = 16.0f;

    float cx = WIN_WIDTH - margin - iconSize * 0.5f;
    float cy = margin + iconSize * 0.5f; // ImGui 坐标 y 向下，顶部按钮排

    pauseButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Pause, Vec2{cx, cy}, iconSize);
    pauseButton->SetOnClick([this](){ isPausing = true; });

    cx -= iconSize + iconSpacing;
    homeButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Home, Vec2{cx, cy}, iconSize);
    homeButton->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
    });

    cx -= iconSize + iconSpacing;
    quitButton = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Quit, Vec2{cx, cy}, iconSize);
    quitButton->SetOnClick([](){
        Application::Get().RequestQuit();
    });

    // ===== 暂停面板按钮（纵向，面板下半部） =====
    const float btnY0 = WIN_HEIGHT * 0.5f + 60.f;   // 继续按钮
    const float gap = 26.f;

    resumeButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f, btnY0},
        Vec2{UITheme::MainBtnW, UITheme::MainBtnH},
        "继续游戏");
    resumeButton->SetOnClick([this](){ isPausing = false; });

    restartButton = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{WIN_WIDTH * 0.5f, btnY0 + UITheme::MainBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "重新开始");
    restartButton->SetOnClick([this](){
        isPausing = false;
        if (onRestartCallback) onRestartCallback();
    });

    homeButtonInPanel = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{WIN_WIDTH * 0.5f, btnY0 + UITheme::MainBtnH * 0.5f + gap * 2 + UITheme::SecondaryBtnH * 1.5f},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "返回主菜单");
    homeButtonInPanel->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
    });
}

void MainSceneUILayer::Reset() {
    pauseButton->Reset();
    homeButton->Reset();
    quitButton->Reset();
    resumeButton->Reset();
    restartButton->Reset();
    homeButtonInPanel->Reset();
    isPausing = false;
}

bool MainSceneUILayer::GetPauseState() const {
    return isPausing;
}

bool MainSceneUILayer::IsPointerOverButtons() const {
    const float mx = Input::GetMouseX();
    const float my = Input::GetMouseY();

    // 与 ButtonSprite::HandleInput 相同的包围盒判定（用按钮自身实时位置/尺寸）
    auto over = [&mx, &my](const std::shared_ptr<ButtonSprite>& btn) {
        if (!btn || !btn->getActive()) return false;
        const Vec2& p = btn->GetPosition();
        const Vec2& s = btn->GetScale();
        return mx > p.x - s.x * 0.5f && mx < p.x + s.x * 0.5f &&
               my > p.y - s.y * 0.5f && my < p.y + s.y * 0.5f;
    };

    if (isPausing) {
        return over(resumeButton) || over(restartButton) || over(homeButtonInPanel);
    }
    return over(pauseButton) || over(homeButton) || over(quitButton);
}
