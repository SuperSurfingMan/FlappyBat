#include "ReadyScene.h"
#include "../sprite/ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include "SceneManager.h"
#include "../Config.h"
#include "../app/Application.h"
#include "../res/ResourceManager.h"
#include "../sprite/BatSprite.h"
#include "../ui/TransitionMask.h"
#include "../Input.h"
#include "MainScene.h"
#include "../app/PlayerProfile.h"
#include "../audio/AudioSystem.h"
#include <cstdio>

ReadyScene::ReadyScene() {
    InitScene();
    std::cout << "ReadyScene created." << std::endl;
}

ReadyScene::~ReadyScene() {}

void ReadyScene::OnEnter() {}
void ReadyScene::OnExit() {}

void ReadyScene::Update(float dt) {
    // ===== 转场遮罩：聚焦完成的同一帧切到主场景（遮罩状态跨场景延续） =====
    TransitionMask::Get().Update(dt);
    if (TransitionMask::Get().IsFocusDone()) {
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::MainScene));
        return;
    }

    // 升级面板：ESC 关闭
    if (upgradePanelOpen && Input::IsKeyPressed(GLFW_KEY_ESCAPE)) {
        upgradePanelOpen = false;
    }

    // 金币不足提示倒计时（升级面板内点击买不起的轨道时触发）
    if (insufficientMsgTimer > 0.0f) {
        insufficientMsgTimer = std::max(0.0f, insufficientMsgTimer - dt);
    }

    // 开始按钮分裂式模式选择（B9 改版）：悬停计时 / 分裂进度 / 模式点击
    UpdateModeSplit(dt);

    // 转场/升级面板打开期间菜单按钮冻结，避免中途点到其它入口；
    // 分裂期间开始按钮（menuButtons[0]）由自定义绘制接管，不参与常规更新
    if (!TransitionMask::Get().IsActive() && !upgradePanelOpen) {
        for (size_t i = 0; i < menuButtons.size(); ++i) {
            if (i == 0 && modeSplitProgress > 0.001f) continue;
            menuButtons[i]->Update(dt);
        }
        achievementEntry->Update(dt);
    }
    UpdateBat(dt);
    UpdateFixedSprites(dt);
    UpdateCandlelights(dt);
}

void ReadyScene::Render(SpriteRenderer& renderer) {
    renderer.Draw_UV(backgroundSprites);
    renderer.Draw_UV(foregroundSprites);
    renderer.Draw_UV(terrainSprites);

    renderer.Draw_UV(centerFrame);
    // 蜡烛氛围光
    renderer.Draw_UV(candleLights);

    // LOGO（顶部，luma key 透明叠加）
    if (logoSprite) {
        renderer.Draw_UV(logoSprite);
    }

    if (batSprite) {
        renderer.Draw_UV(batSprite);
    }

    // 菜单按钮（程序化绘制）；分裂期间开始按钮由 DrawStartButtonRow 接管
    for (size_t i = 0; i < menuButtons.size(); ++i) {
        if (i == 0 && modeSplitProgress > 0.001f) continue;
        menuButtons[i]->DrawLayout();
    }
    if (achievementEntry) achievementEntry->DrawLayout();

    // 开始按钮行：悬停分裂 / 离开收缩动效 + 模式按钮 + 光标旁解释框（B9 改版）
    if (!upgradePanelOpen) {
        DrawStartButtonRow();
        DrawModeTipBox();
    }

    // 护盾升级：右上角金币入口（奖杯左侧）与升级面板（B8）
    if (upgradePanelOpen) {
        DrawUpgradePanelUI();
    } else {
        DrawUpgradeEntryButton();
    }

    // 转场遮罩（最上层）
    TransitionMask::Get().Draw();
}

void ReadyScene::Reset() {
    ResetBatState();
    ResetFixedSpritesState();
    upgradePanelOpen = false;
    insufficientMsgTimer = 0.0f;
    // 分裂式模式选择状态复位
    splitOpen = false;
    modeSplitProgress = 0.0f;
    rowHoverTimer = 0.0f;
    modeHoverIndex = -1;
    modeHoverTimer = 0.0f;
    tipIndex = -1;
    tipFade = 0.0f;
    if (achievementEntry) achievementEntry->Reset();
}

void ReadyScene::InitScene() {
    InitButtons();
    LoadSpritesFromResourceManager();
    CreateSceneSprites();
}

void ReadyScene::InitButtons() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float cx = WIN_WIDTH * 0.5f;

    // ===== 纵向布局（自上而下：开始 > 排行榜 > 设置 > 退出，ImGui 坐标 y 向下递增） =====
    const float gap = 22.f;
    const float startBtnY = StartBtnY; // 开始按钮中心（与分裂式模式选择共用几何）
    float rankBtnY  = startBtnY + UITheme::MainBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f;
    float settingsBtnY = rankBtnY + UITheme::SecondaryBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f;
    float exitBtnY  = settingsBtnY + UITheme::SecondaryBtnH * 0.5f + gap + UITheme::SecondaryBtnH * 0.5f;

    // 开始游戏（主按钮）：悬停 0.2s 横向分裂出三个模式按钮（见 UpdateModeSplit）；
    // 点击也可直接触发分裂（与悬停等效），点击模式按钮后经 StartGameWithMode 启动转场
    auto startBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{cx, startBtnY},
        Vec2{UITheme::MainBtnW, UITheme::MainBtnH},
        "开始游戏");
    startBtn->SetOnClick([this](){
        if (TransitionMask::Get().IsActive() || upgradePanelOpen) return;
        splitOpen = true;
        std::cout << "Start button split by click." << std::endl;
    });
    menuButtons.push_back(startBtn);

    // 排行榜（次级按钮）
    auto rankBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{cx, rankBtnY},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "排行榜");
    rankBtn->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ScoreRankScene));
    });
    menuButtons.push_back(rankBtn);

    // 设置（次级按钮）—— 进入声音设置
    auto settingsBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{cx, settingsBtnY},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "设置");
    settingsBtn->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::SettingsScene));
    });
    menuButtons.push_back(settingsBtn);

    // 退出游戏（次级按钮）
    auto exitBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{cx, exitBtnY},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "退出游戏");
    exitBtn->SetOnClick([](){
        Application::Get().RequestQuit();
    });
    menuButtons.push_back(exitBtn);

    // 右上角成就入口（奖杯图标，B7）：直达成就场景
    achievementEntry = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Rank,
        Vec2{WIN_WIDTH - 52.f, 52.f},
        52.f);
    achievementEntry->SetOnClick([](){
        SceneManager::Instance().ChangeScene(
            Application::Get().GetScene(Application::Scenes::AchievementsScene));
    });
}

void ReadyScene::LoadSpritesFromResourceManager() {
    batSprite = ResourceManager::Get().GetSprite("bat");
    terrainSprites = ResourceManager::Get().GetSpriteList("terrain");
    backgroundSprites = ResourceManager::Get().GetSpriteList("background");
    foregroundSprites = ResourceManager::Get().GetSpriteList("foreground");
}

void ReadyScene::ResetBatState() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    batSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.f);
    auto batSprite_cast = std::static_pointer_cast<BatSprite>(batSprite);
    batSprite_cast->ResetState();
}

void ReadyScene::ResetFixedSpritesState() {
    float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    float terrain_height = Config::GetInstance().GetTerrainHeight();
    float terrain_render_height = Config::GetInstance().GetTerrainRenderHeight();

    float backgroundSpriteHeight = WIN_HEIGHT - terrain_height;
    for (int i = 0; i < 3; i++) {
        backgroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);
        foregroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);
        terrainSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, terrain_render_height * 0.5f);
    }
}

void ReadyScene::UpdateFixedSprites(float dt) {
    const float backgroundSpeed = Config::GetInstance().GetBackgroundSpeed();
    for (auto& bg : backgroundSprites) {
        auto pos = bg->GetPosition();
        pos.x -= backgroundSpeed * dt;
        if (pos.x <= -270.0f * 0.5f) pos.x += 270.0f * 3;
        bg->SetPosition(pos.x, pos.y);
    }
    const float foregroundSpeed = Config::GetInstance().GetForegroundSpeed();
    for (auto& fg : foregroundSprites) {
        auto pos = fg->GetPosition();
        pos.x -= foregroundSpeed * dt;
        if (pos.x <= -270.0f * 0.5f) pos.x += 270.0f * 3;
        fg->SetPosition(pos.x, pos.y);
    }
    const float terrainSpeed = Config::GetInstance().GetObstacleSpeed();
    for (auto& fg : terrainSprites) {
        auto pos = fg->GetPosition();
        pos.x -= terrainSpeed * dt;
        if (pos.x <= -270.0f * 0.5f) pos.x += 270.0f * 3;
        fg->SetPosition(pos.x, pos.y);
    }
}

void ReadyScene::UpdateBat(float dt) {
    batSprite->Update(dt);
}

void ReadyScene::CreateSceneSprites() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // LOGO（上部居中，蝙蝠与月亮构图）
    auto logoTex = ResourceManager::Get().GetTexture2D("v2_ui_logo");
    if (logoTex) {
        logoSprite = std::make_shared<Sprite>(logoTex);
        logoSprite->SetLumaKey(true, 0.06f, 0.12f);
        logoSprite->SetScale(310.f, 310.f);
        logoSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT - 200.f);
    }

    std::shared_ptr<Texture2D> centerFrameTexture = ResourceManager::Get().GetTexture2D("centerFrame");
    centerFrame = std::make_shared<Sprite>(centerFrameTexture);
    centerFrame->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.f);
    centerFrame->SetScale(300.f, 300.f);

    // 蜡烛（保留原有氛围元素，位于屏幕上部两侧）
    std::vector<Vec2> candleLightPositions = {
        Vec2{133.f, 358.f}, Vec2{146.f, 351.f}, Vec2{160.f, 369.f},
        Vec2{377.f, 368.f}, Vec2{391.f, 350.f}, Vec2{406.f, 358.f}
    };
    std::shared_ptr<Texture2D> candleLightSpritesheet = ResourceManager::Get().GetTexture2D("candleLight");
    for (size_t i = 0; i < candleLightPositions.size(); ++i) {
        auto candleLightInstance = std::make_shared<Sprite>();
        candleLightInstance->InitAnimationByTexture(candleLightSpritesheet, 8);
        candleLightInstance->SetScale(10.f, 24.f);
        candleLightInstance->SetPosition(candleLightPositions[i].x, WIN_HEIGHT - candleLightPositions[i].y + 10.f);
        candleLights.push_back(candleLightInstance);
    }
}

void ReadyScene::UpdateCandlelights(float dt) {
    for (const auto& lights : candleLights) {
        lights->Update(dt);
    }
}

// ===== 开始按钮分裂式模式选择（B9 改版） =====

void ReadyScene::UpdateModeSplit(float dt) {
    // 转场期间：冻结当前分裂状态（点击模式按钮后不回缩），不响应悬停/点击
    if (TransitionMask::Get().IsActive()) {
        modeHoverIndex = -1;
        modeHoverTimer = 0.0f;
        tipFade = std::max(0.0f, tipFade - dt * 10.0f);
        if (tipFade <= 0.0f) tipIndex = -1;
        return;
    }

    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float cx = WIN_WIDTH * 0.5f;
    const float mx = Input::GetMouseX();
    const float my = Input::GetMouseY();

    // 升级面板为模态覆盖层：强制收起并跳过交互
    if (upgradePanelOpen) {
        splitOpen = false;
        rowHoverTimer = 0.0f;
        modeHoverIndex = -1;
        modeHoverTimer = 0.0f;
        tipFade = std::max(0.0f, tipFade - dt * 10.0f);
        if (tipFade <= 0.0f) tipIndex = -1;
    } else {
        // 横向区域：三个模式按钮完全展开后的范围（外扩边距），作为"持续分裂"判定区
        const float rowW = ModeSubBtnW * 3.0f + ModeSubBtnGap * 2.0f;
        const bool inRow = mx > cx - rowW * 0.5f - 14.0f && mx < cx + rowW * 0.5f + 14.0f &&
                           my > StartBtnY - UITheme::MainBtnH * 0.5f - 16.0f &&
                           my < StartBtnY + UITheme::MainBtnH * 0.5f + 16.0f;

        // 悬停计时：未分裂时需悬停在"开始游戏"按钮本体上；分裂后整个横向区域均可维持
        const bool overMain = std::abs(mx - cx) < UITheme::MainBtnW * 0.5f &&
                              std::abs(my - StartBtnY) < UITheme::MainBtnH * 0.5f;
        if (splitOpen ? inRow : overMain) {
            rowHoverTimer += dt;
        } else {
            rowHoverTimer = 0.0f;
        }

        // 悬停满阈值 -> 分裂；离开横向区域 -> 收缩
        if (rowHoverTimer >= SplitHoverDelay) splitOpen = true;
        if (!inRow) splitOpen = false;

        // ===== 模式按钮悬停（仅完全分裂后参与判定） =====
        int hoverIdx = -1;
        if (modeSplitProgress >= 0.999f && inRow) {
            for (int i = 0; i < 3; ++i) {
                const float bx = cx + (float)(i - 1) * (ModeSubBtnW + ModeSubBtnGap);
                if (std::abs(mx - bx) < ModeSubBtnW * 0.5f &&
                    std::abs(my - StartBtnY) < UITheme::MainBtnH * 0.5f) {
                    hoverIdx = i;
                    break;
                }
            }
        }

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

        // 点击模式按钮：点击音效 + 直接开始对应模式（无需等解释框出现，保持分裂不回缩）
        if (modeHoverIndex >= 0 && Input::IsMousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Click), 1.0f);
            StartGameWithMode(modeHoverIndex);
        }
    }

    // ===== 解释框：悬停满 0.5s 淡入，离开（换按钮/离区）立即淡出 =====
    if (modeHoverIndex >= 0 && modeHoverTimer >= TipHoverDelay) {
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

    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float cx = WIN_WIDTH * 0.5f;

    const float p = modeSplitProgress;
    const float e = p * p * (3.0f - 2.0f * p); // smoothstep：位置/尺寸插值缓动

    // ===== 主按钮：淡出并轻微收缩，为三个模式按钮让位 =====
    const float mainAlpha = 1.0f - std::clamp(p / 0.55f, 0.0f, 1.0f);
    if (mainAlpha > 0.01f) {
        const float mw = UITheme::MainBtnW * (1.0f - 0.45f * e);
        DrawSplitButtonPlate(cx, StartBtnY, mw, UITheme::MainBtnH, mainAlpha,
                             false, "开始游戏", UITheme::FontButton, mainAlpha);
    }

    // ===== 三个模式按钮：自中心滑出 + 展宽 + 淡入 =====
    const float subAlpha = std::clamp((p - 0.12f) / 0.55f, 0.0f, 1.0f);
    if (subAlpha > 0.01f) {
        static const char* modeNames[3] = { "经典无尽", "限时冲刺", "风暴之夜" };
        for (int i = 0; i < 3; ++i) {
            const float fx = cx + (float)(i - 1) * (ModeSubBtnW + ModeSubBtnGap); // 最终中心
            const float bx = cx + (fx - cx) * e;                                  // 自中心滑出
            const float bw = 46.0f + (ModeSubBtnW - 46.0f) * e;                   // 逐渐展宽
            // 文字随按钮展宽浮现（过窄时先不显示，避免溢出）
            const float textAlpha = subAlpha *
                std::clamp((bw - 70.0f) / (ModeSubBtnW - 70.0f), 0.0f, 1.0f);
            DrawSplitButtonPlate(bx, StartBtnY, bw, UITheme::MainBtnH, subAlpha,
                                 modeHoverIndex == i, modeNames[i], 20.0f, textAlpha);
        }
    }
}

// 模式解释小框：悬停模式按钮 0.5s 后在光标旁淡入，离开即淡出
void ReadyScene::DrawModeTipBox() {
    if (tipFade <= 0.01f || tipIndex < 0) return;

    static const char* modeDescs[3] = {
        "穿越风暴, 挑战最高分",
        "60 秒冲分, 节奏更快",
        "狂风与密集雷暴, 双倍得分的地狱模式",
    };

    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    const float fontSize = 15.0f;
    const std::string text = modeDescs[tipIndex];
    ImVec2 ts = UIBuilder::CalcTextSize(text, fontSize);
    const float padX = 12.0f, padY = 9.0f;
    const float w = ts.x + padX * 2.0f;
    const float h = ts.y + padY * 2.0f;

    // 默认位于光标右侧并垂直居中；越过右边界时翻到左侧
    const float mx = Input::GetMouseX();
    const float my = Input::GetMouseY();
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
    auto& mask = TransitionMask::Get();
    if (mask.IsActive()) return; // 转场进行中不重复启动

    // 保持分裂展开状态（转场期间冻结、不回缩；返回主菜单时由 Reset 复位为单按钮），
    // 仅清除悬停高亮与解释框
    modeHoverIndex = -1;
    modeHoverTimer = 0.0f;
    tipFade = 0.0f;
    tipIndex = -1;

    // 设置主场景模式后走既有的聚焦转场流程
    auto ms = std::static_pointer_cast<MainScene>(Application::Get().GetScene(Application::Scenes::MainScene));
    if (ms) ms->SetMode((MainScene::GameMode)modeIndex);
    std::cout << "Mode selected: " << modeIndex << ", starting transition..." << std::endl;

    // 以蝙蝠当前位置为圆心聚焦（主界面蝙蝠同位同尺寸，营造"同一只蝙蝠"的连续感）
    const auto& p = batSprite->GetPosition();
    mask.StartFocus(p.x, (float)Config::GetInstance().GetScreenHeight() - p.y);
}

// ===== 护盾升级（B8）：右上角金币入口 + 升级面板 =====

void ReadyScene::DrawUpgradeEntryButton() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();

    // 奖杯图标（右上角，中心 WIN_WIDTH-52, 52）左侧的金币入口，与其同行贴顶对齐
    const float w = 176.0f, h = 44.0f;
    const float rightEdge = WIN_WIDTH - 52.0f - 26.0f - 10.0f; // 奖杯左缘再留 10px 间距
    const float cx = rightEdge - w * 0.5f;
    const float cy = 52.0f; // 与奖杯图标垂直居中对齐（贴屏幕顶部）

    const float mx = Input::GetMouseX(), my = Input::GetMouseY();
    const bool hovered = mx > cx - w * 0.5f && mx < cx + w * 0.5f &&
                         my > cy - h * 0.5f && my < cy + h * 0.5f;

    UIBuilder::DrawRoundedRect(cx, cy, w, h, h * 0.5f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(hovered ? UITheme::Amber : UITheme::AmberDim, 0.9f),
        hovered ? 3.0f : 2.0f);

    // 金币图标（双层金色圆，与局内金币 HUD 同款）
    const float icx = cx - w * 0.5f + 22.0f;
    dl->AddCircleFilled(ImVec2(icx + 1, cy + 1), 9.0f,
                        UITheme::ToImU32(UITheme::AmberDim, 0.6f), 16);
    dl->AddCircleFilled(ImVec2(icx, cy), 9.0f, UITheme::ToImU32(UITheme::Amber), 16);
    dl->AddCircle(ImVec2(icx, cy), 5.5f, UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 12, 1.5f);

    if (font) {
        // 持有数（图标右侧，金色）
        std::string coinStr = std::to_string(PlayerProfile::Get().GetCoins());
        dl->AddText(font, 17.0f, ImVec2(icx + 15.0f + 1, cy - 9.0f + 1),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), coinStr.c_str());
        dl->AddText(font, 17.0f, ImVec2(icx + 15.0f, cy - 9.0f),
                    UITheme::ToImU32(UITheme::Amber), coinStr.c_str());

        // 「护盾升级」标签（右对齐）
        const char* label = "护盾升级";
        ImVec2 ls = UIBuilder::CalcTextSize(label, 16.0f);
        dl->AddText(font, 16.0f,
                    ImVec2(cx + w * 0.5f - 14.0f - ls.x + 1, cy - ls.y * 0.5f + 1),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), label);
        dl->AddText(font, 16.0f,
                    ImVec2(cx + w * 0.5f - 14.0f - ls.x, cy - ls.y * 0.5f),
                    UITheme::ToImU32(UITheme::Moonlight, hovered ? 0.95f : 0.6f), label);
    }

    // 点击进入升级面板（转场进行中不响应）
    if (hovered && !TransitionMask::Get().IsActive() &&
        Input::IsMousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        upgradePanelOpen = true;
        insufficientMsgTimer = 0.0f;
        AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Click), 1.0f);
        std::cout << "Upgrade panel opened." << std::endl;
    }
}

void ReadyScene::DrawUpgradePanelUI() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    const float cx = WIN_WIDTH * 0.5f;

    auto& profile = PlayerProfile::Get();

    // 全屏暗色遮罩（与模式选择覆盖层同款）
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(WIN_WIDTH, WIN_HEIGHT),
                      ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.05f, 0.78f)));

    // 标题 + 金币余额
    UIBuilder::DrawCenteredText("护盾升级", cx, 128.0f, 34.0f,
                                UITheme::ToImU32(UITheme::Amber));
    {
        const float pillW = 200.0f, pillH = 44.0f;
        const float pcy = 180.0f;
        UIBuilder::DrawRoundedRect(cx, pcy, pillW, pillH, pillH * 0.5f,
            UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.9f),
            UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);
        const float icx = cx - 58.0f;
        dl->AddCircleFilled(ImVec2(icx + 1, pcy + 1), 10.0f,
                            UITheme::ToImU32(UITheme::AmberDim, 0.6f), 16);
        dl->AddCircleFilled(ImVec2(icx, pcy), 10.0f, UITheme::ToImU32(UITheme::Amber), 16);
        dl->AddCircle(ImVec2(icx, pcy), 6.0f, UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 12, 1.5f);
        UIBuilder::DrawCenteredText(std::to_string(profile.GetCoins()),
                                    cx + 14.0f, pcy, 22.0f,
                                    UITheme::ToImU32(UITheme::Amber));
    }

    // 金币不足提示（点击买不起的轨道时短暂显示）
    if (insufficientMsgTimer > 0.0f) {
        UIBuilder::DrawCenteredText("金币不足！", cx, 228.0f, 20.0f,
                                    UITheme::ToImU32(UITheme::Blood));
    }

    const float mx = Input::GetMouseX(), my = Input::GetMouseY();
    const bool clicked = Input::IsMousePressed(GLFW_MOUSE_BUTTON_LEFT);

    // ===== 两条升级轨道卡片 =====
    // 展示数值与 PlayerProfile / MainScene 的升级参数保持一致：
    // 持续时间 3.0s + 0.4s/级；充能速度 ×1.0 + 10%/级
    static const char* trackNames[PlayerProfile::TrackCount] = { "持续时间", "充能速度" };
    static const char* trackDescs[PlayerProfile::TrackCount] = {
        "护盾无敌时间 · 每级 +0.4 秒 · 满级 5.0 秒",
        "能量获取效率 · 每级 +10% · 满级 ×1.5",
    };

    for (int t = 0; t < PlayerProfile::TrackCount; ++t) {
        const float cardW = 452.0f, cardH = 180.0f;
        const float cy = 330.0f + (float)t * 198.0f;
        const float top = cy - cardH * 0.5f;
        const float left = cx - cardW * 0.5f;
        const float right = cx + cardW * 0.5f;

        // 卡片底板（与模式选择卡片同款石板质感）
        UIBuilder::DrawButtonPlate(cx, cy, cardW, cardH, 14.0f,
            UITheme::ToImU32(UITheme::NightBg, 0.95f),
            UITheme::ToImU32(UITheme::NightDeep, 0.95f),
            UITheme::ToImU32(UITheme::AmberDim), 2.0f);

        const int lv = profile.GetLevel(t);
        const bool maxed = lv >= PlayerProfile::MaxLevel;
        const long long cost = profile.NextCost(t); // 满级时为 -1

        // 名称（左） + 当前值（右）
        if (font) {
            const float nameY = top + 34.0f;
            dl->AddText(font, 26.0f, ImVec2(left + 24.0f + 2, nameY - 13.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), trackNames[t]);
            dl->AddText(font, 26.0f, ImVec2(left + 24.0f, nameY - 13.0f),
                        UITheme::ToImU32(UITheme::Amber), trackNames[t]);

            char valBuf[32];
            if (t == PlayerProfile::ShieldTrack::Duration) {
                snprintf(valBuf, sizeof(valBuf), "%.1f 秒", 3.0f + 0.4f * (float)lv);
            } else {
                snprintf(valBuf, sizeof(valBuf), "×%.1f", 1.0f + 0.1f * (float)lv);
            }
            ImVec2 ts = UIBuilder::CalcTextSize(valBuf, 26.0f);
            dl->AddText(font, 26.0f, ImVec2(right - 24.0f - ts.x + 2, nameY - 13.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), valBuf);
            dl->AddText(font, 26.0f, ImVec2(right - 24.0f - ts.x, nameY - 13.0f),
                        UITheme::ToImU32(UITheme::Moonlight), valBuf);

            // 说明行
            dl->AddText(font, 16.0f, ImVec2(left + 24.0f + 2, top + 58.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.7f)), trackDescs[t]);
            dl->AddText(font, 16.0f, ImVec2(left + 24.0f, top + 58.0f),
                        UITheme::ToImU32(UITheme::Moonlight, 0.6f), trackDescs[t]);
        }

        // 等级刻度：5 个圆点（已升级的实心金色）
        const float pipSpacing = 30.0f;
        const float pipY = top + 98.0f;
        for (int i = 0; i < PlayerProfile::MaxLevel; ++i) {
            const float px = cx + ((float)i - (PlayerProfile::MaxLevel - 1) * 0.5f) * pipSpacing;
            if (i < lv) {
                dl->AddCircleFilled(ImVec2(px + 1, pipY + 1), 7.0f,
                                    UITheme::ToImU32(UITheme::AmberDim, 0.5f), 12);
                dl->AddCircleFilled(ImVec2(px, pipY), 7.0f,
                                    UITheme::ToImU32(UITheme::Amber), 12);
            } else {
                dl->AddCircleFilled(ImVec2(px, pipY), 7.0f,
                                    UITheme::ToImU32({0.05f, 0.04f, 0.09f}), 12);
                dl->AddCircle(ImVec2(px, pipY), 7.0f,
                              UITheme::ToImU32(UITheme::AmberDim, 0.6f), 12, 2.0f);
            }
        }

        // 购买按钮：可买（主按钮高亮）/ 买不起（压暗）/ 满级（置灰）
        const float btnW = 240.0f, btnH = 52.0f;
        const float bx = cx, by = top + 140.0f;
        const bool affordable = !maxed && profile.GetCoins() >= cost;
        const bool btnHover = affordable &&
            std::abs(mx - bx) < btnW * 0.5f && std::abs(my - by) < btnH * 0.5f;
        const std::string buyText = maxed ? "已满级"
                                    : "升级 · " + std::to_string(cost) + " 金币";

        if (maxed) {
            UIBuilder::DrawButtonPlate(bx, by, btnW, btnH, 14.0f,
                UITheme::ToImU32({0.10f, 0.09f, 0.14f}, 0.9f),
                UITheme::ToImU32(UITheme::NightDeep, 0.9f),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f), 2.0f);
            UIBuilder::DrawButtonText(buyText, bx, by, 22.0f, false, false);
        } else if (affordable) {
            UIBuilder::DrawButton(bx, by, btnW, btnH, true, btnHover, false);
            UIBuilder::DrawButtonText(buyText, bx, by, 20.0f, btnHover, false);
        } else {
            UIBuilder::DrawButtonPlate(bx, by, btnW, btnH, 14.0f,
                UITheme::ToImU32({0.12f, 0.10f, 0.16f}, 0.9f),
                UITheme::ToImU32(UITheme::NightDeep, 0.9f),
                UITheme::ToImU32(UITheme::AmberDim, 0.7f), 2.0f);
            if (font) {
                ImVec2 ts = UIBuilder::CalcTextSize(buyText, 20.0f);
                dl->AddText(font, 20.0f, ImVec2(bx - ts.x * 0.5f + 2, by - ts.y * 0.5f + 2),
                            ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), buyText.c_str());
                dl->AddText(font, 20.0f, ImVec2(bx - ts.x * 0.5f, by - ts.y * 0.5f),
                            UITheme::ToImU32(UITheme::Moonlight, 0.45f), buyText.c_str());
            }
        }

        // 购买点击：成功播放奖励音效（购买即自动存盘）；失败短暂提示金币不足
        if (!maxed && clicked &&
            std::abs(mx - bx) < btnW * 0.5f && std::abs(my - by) < btnH * 0.5f) {
            if (profile.TryBuy(t)) {
                AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Record), 1.0f);
                std::cout << "Shield track " << t << " upgraded to level "
                          << profile.GetLevel(t) << "." << std::endl;
            } else {
                insufficientMsgTimer = 1.5f;
            }
        }
    }

    // 返回按钮（关闭面板回到主菜单）
    const float backY = 672.0f;
    const bool backHovered = std::abs(mx - cx) < 100.0f && std::abs(my - backY) < 30.0f;
    UIBuilder::DrawButton(cx, backY, 200.0f, 60.0f, false, backHovered, false);
    UIBuilder::DrawButtonText("返回", cx, backY, 24.0f, backHovered, false);
    if (backHovered && clicked) {
        upgradePanelOpen = false;
    }
}
