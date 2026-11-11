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

// 布局常量集中一处（ReadySceneLayout.h），这里只补上"模式数量从注册表来"这一层推导
using namespace ready::layout;

float ModeOffset(int i) {
    return ModeOffsetX(i, ModeRegistry::Instance().Size());
}

float RowWidth() {
    return ModeRowWidth(ModeRegistry::Instance().Size());
}

} // namespace

// =============================================================================
// 主菜单场景：生命周期、场景布景（背景/蜡烛/LOGO/待机蝙蝠）与控件登记。
//
// 第 5 期拆分后本文件只留"场景本身"：
//   ReadySceneMenu.cpp    开始按钮的分裂式模式选择（悬停计时/动效/解释框/启动）
//   ReadySceneUpgrade.cpp 护盾升级入口与面板（金币/等级/购买）
//   ReadySceneLayout.h    布局常量与几何公式
// =============================================================================

ReadyScene::ReadyScene(Services& services) : svc_(services) {
    // 声明本场景关注的快捷键（ESC 关升级面板）：交由 UISystem 采集后推送
    svc_.UI().WatchKeys({GLFW_KEY_ESCAPE});

    InitScene();
    std::cout << "ReadyScene created." << std::endl;
}

ReadyScene::~ReadyScene() {
    // 兼底注销（正常已由 OnExit 注销）：退出路径若跳过 OnExit 则防悬针 sink
    svc_.UI().RemoveGameInputSink(this);
}

void ReadyScene::OnEnter() {
    // 本场景控件组接管交互（转场期间由 Update 逐帧关闭）
    menuScope.SetEnabled(true);
    upgradeScope.SetEnabled(upgradePanelOpen);
    // 订阅推送式输入（本场景只用 ESC 关闭升级面板）
    svc_.UI().AddGameInputSink(this);
}

void ReadyScene::OnExit() {
    // 关闭本场景全部控件组：避免离开主菜单后残留热区继续抢点
    menuScope.SetEnabled(false);
    upgradeScope.SetEnabled(false);
    // 停止输入推送
    svc_.UI().RemoveGameInputSink(this);
}

void ReadyScene::Update(float dt) {
    // 转场驱动与遮蔽中点切幕已上收 SceneManager（TickTransition）；本场景只查询状态做本地闸门
    const bool transitioning = svc_.Scenes().IsTransitionActive();

    // 升级面板：ESC 关闭（读 UISystem 推送的快照，不再直接询设备）
    if (upgradePanelOpen && gameInput.Pressed(GLFW_KEY_ESCAPE)) {
        upgradePanelOpen = false;
    }

    // ===== UI 交互闸门 =====
    // 转场进行中主菜单不可点（UISystem 侧另有全屏闸门，此处同步关闭组兜底）；
    // 升级面板弹开时以模态独占命中
    menuScope.SetEnabled(!transitioning);
    upgradeScope.SetEnabled(upgradePanelOpen);

    // 金币不足提示倒计时（升级面板内点击买不起的轨道时触发）
    if (insufficientMsgTimer > 0.0f) {
        insufficientMsgTimer = std::max(0.0f, insufficientMsgTimer - dt);
    }

    // 开始按钮分裂式模式选择（B9 改版）：悬停计时 / 分裂进度 / 模式点击
    UpdateModeSplit(dt);

    // 转场/升级面板打开期间菜单按钮冻结，避免中途点到其它入口；
    // 分裂期间开始按钮（menuButtons[0]）由自定义绘制接管，不参与常规更新
    if (!transitioning && !upgradePanelOpen) {
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

    // 转场遮罩由 SceneManager 于场景绘制后统一叠加（DrawTransitionOverlay），
    // 本场景不再认识任何具体转场
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
    // 上一轮留下的悬停/按下视觉一并清除（注册关系不变）
    svc_.UI().ClearWidgetState(menuGroupId);
    svc_.UI().ClearWidgetState(upgradeGroupId);
}

void ReadyScene::InitScene() {
    InitButtons();
    LoadSpritesFromResourceManager();
    CreateSceneSprites();
}

void ReadyScene::InitButtons() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float cx = WIN_WIDTH * 0.5f;

    // UI 组：主菜单控件（非模态）+ 护盾升级面板（模态）
    // 注册顺序即层叠顺序：后注册者在上（模式按钮 > 分裂行区域 > 菜单按钮）
    UISystem& ui = svc_.UI();
    menuGroupId    = ui.CreateGroup(kLayerMenu);
    upgradeGroupId = ui.CreateGroup(kLayerUpgradePanel);
    menuScope.Bind(ui, menuGroupId, false);
    upgradeScope.Bind(ui, upgradeGroupId, true);

    // ===== 纵向布局（自上而下：开始 > 排行榜 > 设置 > 退出，ImGui 坐标 y 向下递增） =====
    const float gap = 22.f;
    const float startBtnY = kStartBtnY; // 开始按钮中心（与分裂式模式选择共用几何）
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
        if (svc_.Scenes().IsTransitionActive() || upgradePanelOpen) return;
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
    rankBtn->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneScoreRank);
    });
    menuButtons.push_back(rankBtn);

    // 设置（次级按钮）—— 进入声音设置
    auto settingsBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{cx, settingsBtnY},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "设置");
    settingsBtn->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneSettings);
    });
    menuButtons.push_back(settingsBtn);

    // 退出游戏（次级按钮）
    auto exitBtn = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{cx, exitBtnY},
        Vec2{UITheme::SecondaryBtnW, UITheme::SecondaryBtnH},
        "退出游戏");
    exitBtn->SetOnClick([this]() {
        svc_.App().RequestQuit();
    });
    menuButtons.push_back(exitBtn);

    // 右上角成就入口（奖杯图标，B7）：直达成就场景
    achievementEntry = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Rank,
        Vec2{WIN_WIDTH - 52.f, 52.f},
        52.f);
    achievementEntry->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneAchievements);
    });

    // ===== 菜单按钮登记到 UISystem（传 shared_ptr，登记即共享持有） =====
    for (auto& btn : menuButtons) ui.Add(menuGroupId, btn);
    ui.Add(menuGroupId, achievementEntry);

    // ===== 分裂行热区：维持展开的整行横向范围（只取悬停事实，不绘制不发声） =====
    const float rowW = RowWidth();
    startRowZone->SetRect(UIRect::FromCenter(cx, kStartBtnY,
                                            rowW + kRowZonePadX * 2.0f,
                                            UITheme::MainBtnH + kRowZonePadY * 2.0f));
    startRowZone->SetPlayFeedbackSFX(false);
    ui.Add(menuGroupId, startRowZone);

    // ===== 模式按钮热区：分裂完成后最终几何即此矩形（DrawStartButtonRow 共用 ModeOffsetX） =====
    modeZones.resize(ModeRegistry::Instance().Size()); // 数量由注册表动态决定
    for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i) {
        modeZones[i] = std::make_shared<UIWidget>();
        const float bx = cx + ModeOffset(i) * (kModeSubBtnW + kModeSubBtnGap);
        modeZones[i]->SetRect(UIRect::FromCenter(bx, kStartBtnY, kModeSubBtnW, UITheme::MainBtnH));
        modeZones[i]->SetOnClick([this, i]() { StartGameWithMode(i); });
        ui.Add(menuGroupId, modeZones[i]);
    }

    // ===== 护盾升级：右上角金币入口（属主菜单组） =====
    coinEntry->SetOnClick([this]() {
        upgradePanelOpen = true;
        insufficientMsgTimer = 0.0f;
        std::cout << "Upgrade panel opened." << std::endl;
    });
    ui.Add(menuGroupId, coinEntry);

    // ===== 升级面板内控件（模态组）：矩形在绘制函数里与底板共用同一份几何逐帧同步 =====
    for (int t = 0; t < PlayerProfile::TrackCount; ++t) {
        buyZones[t] = std::make_shared<UIWidget>();
        buyZones[t]->SetPlayFeedbackSFX(false); // 购买结果自带音效（成功=奖励音）
        buyZones[t]->SetOnClick([this, t]() {
            auto& profile = svc_.Profile();
            if (profile.GetLevel(t) >= PlayerProfile::MaxLevel) return; // 已满级：不响应
            if (profile.TryBuy(t)) {
                svc_.Audio().PlaySFX(SFXToFile(SFX::Record), 1.0f);
                std::cout << "Shield track " << t << " upgraded to level "
                          << profile.GetLevel(t) << "." << std::endl;
            } else {
                insufficientMsgTimer = 1.5f;
            }
        });
        ui.Add(upgradeGroupId, buyZones[t]);
    }

    upgradeBackZone->SetPlayFeedbackSFX(false);
    upgradeBackZone->SetOnClick([this]() { upgradePanelOpen = false; });
    ui.Add(upgradeGroupId, upgradeBackZone);
}

void ReadyScene::LoadSpritesFromResourceManager() {
    batSprite = svc_.Res().GetSprite("bat");
    terrainSprites = svc_.Res().GetSpriteList("terrain");
    backgroundSprites = svc_.Res().GetSpriteList("background");
    foregroundSprites = svc_.Res().GetSpriteList("foreground");
}

void ReadyScene::ResetBatState() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    batSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.f);
    auto batSprite_cast = std::static_pointer_cast<BatSprite>(batSprite);
    batSprite_cast->ResetState();
}

void ReadyScene::ResetFixedSpritesState() {
    float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    float terrain_height = svc_.Cfg().GetTerrainHeight();
    float terrain_render_height = svc_.Cfg().GetTerrainRenderHeight();

    float backgroundSpriteHeight = WIN_HEIGHT - terrain_height;
    for (int i = 0; i < 3; i++) {
        backgroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);
        foregroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);
        terrainSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, terrain_render_height * 0.5f);
    }
}

void ReadyScene::UpdateFixedSprites(float dt) {
    const float backgroundSpeed = svc_.Cfg().GetBackgroundSpeed();
    for (auto& bg : backgroundSprites) {
        auto pos = bg->GetPosition();
        pos.x -= backgroundSpeed * dt;
        if (pos.x <= -270.0f * 0.5f) pos.x += 270.0f * 3;
        bg->SetPosition(pos.x, pos.y);
    }
    const float foregroundSpeed = svc_.Cfg().GetForegroundSpeed();
    for (auto& fg : foregroundSprites) {
        auto pos = fg->GetPosition();
        pos.x -= foregroundSpeed * dt;
        if (pos.x <= -270.0f * 0.5f) pos.x += 270.0f * 3;
        fg->SetPosition(pos.x, pos.y);
    }
    const float terrainSpeed = svc_.Cfg().GetObstacleSpeed();
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
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    // LOGO（上部居中，蝙蝠与月亮构图）
    auto logoTex = svc_.Res().GetTexture2D("v2_ui_logo");
    if (logoTex) {
        logoSprite = std::make_shared<Sprite>(logoTex);
        logoSprite->SetLumaKey(true, 0.06f, 0.12f);
        logoSprite->SetScale(310.f, 310.f);
        logoSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT - 200.f);
    }

    std::shared_ptr<Texture2D> centerFrameTexture = svc_.Res().GetTexture2D("centerFrame");
    centerFrame = std::make_shared<Sprite>(centerFrameTexture);
    centerFrame->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.f);
    centerFrame->SetScale(300.f, 300.f);

    // 蜡烛（保留原有氛围元素，位于屏幕上部两侧）
    std::vector<Vec2> candleLightPositions = {
        Vec2{133.f, 358.f}, Vec2{146.f, 351.f}, Vec2{160.f, 369.f},
        Vec2{377.f, 368.f}, Vec2{391.f, 350.f}, Vec2{406.f, 358.f}
    };
    std::shared_ptr<Texture2D> candleLightSpritesheet = svc_.Res().GetTexture2D("candleLight");
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
