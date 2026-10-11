#include "MainScene.h"
#include "../app/Application.h"

#include "../sprite/Sprite.h"
#include "../sprite/BatSprite.h"
#include "../sprite/CollectableSprite.h"

#include "../Config.h"

#include "../res/ResourceManager.h"
#include "../audio/AudioSystem.h"
#include <GLFW/glfw3.h>   // 仅取 GLFW_KEY_* / GLFW_MOUSE_BUTTON_* 常量（输入由 UISystem 推送）
#include "imgui.h"
#include "SceneManager.h"
#include "ReadyScene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

#include "../layer/MainSceneUILayer.h"
#include "../layer/MainSceneOverLayer.h"
#include "../layer/ToastLayer.h"
#include "../font/FontManager.h"
#include "../app/ScoreRank.h"
#include "../app/Achievements.h"
#include "../app/PlayerProfile.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../ui/UISystem.h"
#include "../ui/TransitionMask.h"

namespace {
// 局内 HUD 交互组层叠：低于暂停面板(40)/结算面板(50)，与顶部图标排(10) 同时参与命中
constexpr int kLayerMainHud = 12;
}

// GameMode -> ScoreRank::RankMode：两枚举成员一一对应，显式映射避免隐式耦合
static ScoreRank::RankMode ToRankMode(MainScene::GameMode m) {
    switch (m) {
        case MainScene::GameMode::TimeRush:   return ScoreRank::RankMode::TimeRush;
        case MainScene::GameMode::StormNight: return ScoreRank::RankMode::StormNight;
        case MainScene::GameMode::Classic:    break;
    }
    return ScoreRank::RankMode::Classic;
}

MainScene::MainScene()
{
    LoadSpritesFromResourceManager();

    // ===== 道具回调（A2 激活 + A3 连击 + B8 货币）：效果全部来自 EffectOf 参数表 =====
    // 蚊子直接 +1 分；萤火虫充 1/6 能量；猕猴桃充 1/3 能量；
    // 三者均拾取金币并参与连击（窗口内连续拾取，从第 2 个起获得额外加分）
    for (int type = 0; type < ItemTypeCount; ++type) {
        const ItemEffect fx = EffectOf(type);
        for (auto& sp : *ItemPool(type)) {
            std::static_pointer_cast<CollectableSprite>(sp)->SetCallback([this, fx]() {
                if (fx.score > 0) AddScore((size_t)fx.score); // 统一入口：含模式倍率与成就上报
                if (fx.energy > 0.0f) AddEnergy(fx.energy);
                PlayerProfile::Get().AddCoins(fx.coins); // 局内货币（B8）
                ReportAchievement(AchievementEvent::ItemCollected, 1);
                SpawnComboBonusText(ApplyPickupComboBonus());
                SpawnFloatText(fx.floatText, bat->GetPosition(), fx.r, fx.g, fx.b, fx.fontSize);
            });
        }
    }

    // 初始化渲染器（绑定雨/闪电数据源；shader 已在资源加载阶段预编译）
    rainRenderer.Init(rainSystem);
    lightningRenderer.Init(rainSystem);
    windRenderer.Init(); // 风线（阵风可视化；数据自持，仅吃 Update 的阵风状态）

    // 怪物（EnemyDirector 框架）：登记内置怪物并初始化（纹理已在资源加载阶段就绪）
    RegisterBuiltinEnemies(enemies);
    enemies.InitAll();

    // 天色变暗遮罩（A4-c）：白色纹理着色，透明度随难度档位渐变
    auto whiteTex = ResourceManager::Get().GetTexture2D("white");
    if (whiteTex) {
        stormScrimSprite = std::make_shared<Sprite>(whiteTex);
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, 0.0f);
        stormScrimSprite->SetScale(Config::GetInstance().GetScreenWidth() + 4.0f,
                                   Config::GetInstance().GetScreenHeight() + 4.0f);
        stormScrimSprite->SetPosition(Config::GetInstance().GetScreenWidth() * 0.5f,
                                      Config::GetInstance().GetScreenHeight() * 0.5f);
    }

    mainSceneUILayer = std::make_shared<MainSceneUILayer>();
    mainSceneOverLayer = std::make_shared<MainSceneOverLayer>();

    // ===== 局内 HUD 交互控件：能量图标热区 =====
    // 命中矩形与绘制几何同源（GetEnergyIconRect）；可点与否由 SetEnabled 逐帧同步，
    // 鼠标判定不再出现在本场景代码里
    hudGroupId = UISystem::Get().CreateGroup(kLayerMainHud);
    energyIcon.SetPlayFeedbackSFX(false); // 原实现无音效，保持听感不变
    energyIcon.SetOnClick([this]() { TryReleaseShield(); });
    UISystem::Get().Add(hudGroupId, &energyIcon);

    // 玩法操控的按键集：声明给 UISystem，本场景只读它推送的回帧快照
    UISystem::Get().WatchKeys({GLFW_KEY_SPACE, GLFW_KEY_W, GLFW_KEY_UP,
                               GLFW_KEY_S, GLFW_KEY_DOWN,
                               GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
                               GLFW_KEY_F});

    // 成就解锁提示（底部居中，带专属面板背景；任何阶段都可能弹出）
    auto achToast = std::make_shared<ToastLayer>(2.5f, "", Vec3{0.941f, 0.722f, 0.314f}, 26.0f,
                                                 Vec2{270.0f, 838.0f});
    if (auto tex = ResourceManager::Get().GetTexture2D("v2_ui_achievement_toast")) {
        achToast->SetBackground(tex);
    }
    achievementToast = achToast;

    // 巨化星精灵（ResourceManager 程序化生成；默认未激活）
    giantOrb = ResourceManager::Get().GetSprite("giantOrb");

    // 暂停面板的"重新开始"：走 3-2-1 倒数流程（无转场遮罩），倒数结束统一给予无敌
    std::static_pointer_cast<MainSceneUILayer>(mainSceneUILayer)->SetRestartCallback([this](){
        Reset();
        OnEnter();
    });

    // GameOver 重新开始：同上，倒数后开局
    std::static_pointer_cast<MainSceneOverLayer>(mainSceneOverLayer)->SetRestartEvent([this](){
        mainSceneOverLayer->Reset();
        Reset();
        OnEnter();
    });
}

MainScene::~MainScene() 
{

}

void MainScene::OnEnter()
{
    // 接管玩法输入推送（OnExit 注销：非当前场景不应再收到回帧）
    // 同时为 UI 图层注册一份订阅：它只读暂停快捷键（ESC/P/R），与玩法 sink 并存
    UISystem& ui = UISystem::Get();
    ui.AddGameInputSink(this);
    ui.AddGameInputSink(std::static_pointer_cast<MainSceneUILayer>(mainSceneUILayer).get());

    // 重新进入本场景：恢复 HUD 与结算面板的交互能力（OnExit 处已关闭）
    std::static_pointer_cast<MainSceneOverLayer>(mainSceneOverLayer)->SetInteractive(true);

    // 开始循环播放雨声环境音（均匀、平缓）
    AudioSystem::Instance().PlayAmbient("assets/Audio/Sound Effects/Rain.wav", true, 0.4f);
    rainSystem->SetLightningWarnLead(params().lightningWarnLead); // 落雷预警提前量（随模式差异化）
    rainSystem->StartRandomLightning(5.0f, 10.0f);
    ApplyWeatherForTier(GetCurrentDifficultyTier()); // 天气基线（A4-c；场景切换前已 Reset，此处为第 0 档）

    // 风暴之夜：天气状态常驻消息行（天气事件消息盒）
    if (params().stormWeather) {
        PostWeatherMsg(WMsgStorm, "风暴之夜", 0.8f, true);
    }

    // 入场流程：从准备界面"开始游戏"进入时转场遮罩仍在（先揭示后倒数）；
    // 局内重开无遮罩，直接 3-2-1 倒数。开局无敌改由倒数结束时统一给予。
    if (TransitionMask::Get().IsActive()) {
        introPhase = IntroPhase::Mask;
    } else {
        introPhase = IntroPhase::Counting;
        countdownValue = 3;
        introTimer = 0.0f;
        AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Countdown), 0.5f);
    }
}

void MainScene::OnExit()
{
    // 停止雨声环境音
    AudioSystem::Instance().StopAmbient();

    // 停止玩法与 UI 快捷键的输入推送
    UISystem& ui = UISystem::Get();
    ui.RemoveGameInputSink(this);
    ui.RemoveGameInputSink(std::static_pointer_cast<MainSceneUILayer>(mainSceneUILayer).get());

    // 局内 UI 交互全部关闭：本场景控件残留在 UISystem 中会在其它场景抢下点击
    SetHudInteractive(false);
    std::static_pointer_cast<MainSceneOverLayer>(mainSceneOverLayer)->SetInteractive(false);

    // 离开主场景（暂停面板"返回主菜单"、右上角 Home、程序退出等）时，
    // 自动把本局分数写入当前模式的排行榜；已保存过（死亡结算）则跳过
    if (!scoreSavedThisRun) {
        ScoreRank::Get().UpdateScore(obstacles_score, ToRankMode(mode));
        scoreSavedThisRun = true;
        std::cout << "Exit MainScene: score " << obstacles_score
                  << " saved to rank (" << ScoreRank::ModeName(ToRankMode(mode)) << ")." << std::endl;
    }
    PlayerProfile::Get().Save(); // 中途退出也保存金币与升级（局末结算处已另存）
    Achievements::Get().Save(); // 同步落盘累计统计（成就解锁时已即存，此处兜底防中途退出丢失）
}

void MainScene::Update(float dt)
{
    // 击杀慢动作（C12 联动）：先以真实 dt 递减计时，再整帧缩放——动画/物理/粒子同步放缓，
    // 输入为边沿检测不受影响；缩放随剩余时间线性缓升，避免硬切回常速。
    if (slowMoTimer > 0.0f) slowMoTimer = std::max(0.0f, slowMoTimer - dt);
    dt *= TimeScale();

    // 成就解锁提示（任何阶段都可能弹出）
    PollAchievementToasts();
    achievementToast->Update(dt);

    // 天气事件消息盒：闪烁计时 + 雷击事件轮询（阵风/风暴档位在各自更新点驱动）
    UpdateWeatherFeed(dt);

    // ===== UI 交互闸门：入场倒数/结算期间局内按钮与能量图标一律不响应 =====
    // （结算面板自己的按钮组由 OverLayer 按 layerActive 管理，不受本闸门影响）
    const bool uiInteractive = !gameOver && introPhase == IntroPhase::None;
    SetHudInteractive(uiInteractive);
    if (uiInteractive) mainSceneUILayer->Update(dt);

    if (gameOver) {
        mainSceneOverLayer->Update(dt);
        return;
    }

    // ===== 入场流程：转场遮罩 / 倒数期间，世界与操控冻结 =====
    // 障碍不进入屏幕、蝙蝠原地待机振翅（与准备界面同一姿态），天气照常以保持氛围
    if (introPhase != IntroPhase::None) {
        UpdateIntro(dt);
        rainSystem->SetScrollSpeed(0.0f); // 世界静止，水花与冻结地面同步
        rainSystem->Update(dt);
        bat->Update(dt);
        return;
    }

    // collisionEnabled 语义: 碰撞是否发生
    if (std::static_pointer_cast<MainSceneUILayer>(mainSceneUILayer)->GetPauseState() || !collisionEnabled) {
        rainSystem->SetScrollSpeed(0.0f);
        if (!collisionEnabled) {
            rainSystem->Update(dt);
            bat->Update(dt);
        }
        return;
    }

    UpdateBackground(dt);
    UpdateForeground(dt);
    UpdateTerrain(dt);
    UpdateObstacles(dt);
    UpdateFlyingObstacles(dt); // 被撞飞障碍的抛物线飞行与回收（B6-b）
    UpdateItems(dt);
    UpdateOrb(dt);       // 巨化星（B6-b）
    UpdateGiant(dt);     // 巨化计时与撞飞判定（B6-b）
    // 输入意图（由 UISystem 推送的本帧快照）：俯冲（S/↓，B6-a；急坠移动语义由场景实现，Boss 战接触判定由怪物侧完成）
    const bool diveRequested = gameInput.Pressed(GLFW_KEY_S) ||
                               gameInput.Pressed(GLFW_KEY_DOWN);
    UpdateGust(dt); // 阵风（A4-b）：先修改垂直速度，再由 UpdateBat 积分位置
    windRenderer.Update(gustTimer > 0.0f, gustForce, dt); // 风线可视化（仅生效期显示；预警期无气流）
    UpdateBat(dt, diveRequested);
    UpdateShake(dt);      // 全局抖动衰减（B6-b 反馈）
    UpdateTimeRush(dt);   // 限时冲刺倒计时（B9）
    // 地面滚动/水花漂移速度与障碍物同步（难度递进后不再恒为 200）
    rainSystem->SetScrollSpeed(GetCurrentObstacleSpeed());
    rainSystem->Update(dt);

    // ===== 能量护盾 =====
    uiPulseTimer += dt;
    if (shieldTimer > 0.0f) shieldTimer = std::max(0.0f, shieldTimer - dt);
    // 能量充满后按空格释放护盾
    if (gameInput.Pressed(GLFW_KEY_SPACE)) {
        TryReleaseShield();
    }

    // ===== 连击窗口倒计时（A3） =====
    if (comboTimer > 0.0f) {
        comboTimer = std::max(0.0f, comboTimer - dt);
        if (comboTimer <= 0.0f) comboCount = 0; // 窗口耗尽，连击断掉
    }
    UpdateFloatingTexts(dt);

    // ===== 难度跨档提示（A1）+ 天气联动（A4-c） =====
    {
        int tier = GetCurrentDifficultyTier();
        if (tier > lastTier) {
            lastTier = tier;
            ApplyWeatherForTier(tier); // 雨强/雷频/天色随档位变化
            const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
            const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
            SpawnFloatText("风暴渐强 · 提速！",
                           Vec2(WIN_WIDTH * 0.5f, WIN_HEIGHT - 70.0f),
                           UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 28.0f, 2.0f);
            // 消息盒：风暴档位为持续状态（本局内不回落），短暂闪烁后常驻显示（天气事件消息盒）
            PostWeatherMsg(WMsgStorm, "风暴 " + std::to_string(tier) + " 级", 0.8f, true);
        }
    }

    // ===== 天色遮罩渐变（A4-c）；段落期额外压暗（怪物 scrimBoost，§9 +0.10） =====
    const float scrimTarget = std::clamp(stormScrimTargetAlpha + enemies.Rules().scrimBoost, 0.0f, 1.0f);
    if (stormScrimAlpha != scrimTarget) {
        float step = dt * 0.25f;
        stormScrimAlpha += std::clamp(scrimTarget - stormScrimAlpha, -step, step);
    }

    // ===== 段落期雨强下限（怪物 minRainIntensity，§9：遭遇期抬到 ≥80%） =====
    // 逐帧以 max(档位基线, 段落下限) 重设目标：RainSystem 内部渐变，段落结束自然回落基线
    rainSystem->SetRainIntensity(std::max(weatherRainBaseline, enemies.Rules().minRainIntensity));

    // ===== 怪物（EnemyDirector）：上下文下行 / 输入意图 / 每帧驱动 =====
    // 暂停/入场/死亡动画期间随上方门控不推进（Update 早退即冻结）
    {
        EnemyContext ectx;
        ectx.batPos = bat->GetPosition();
        // 等效碰撞半径=横向尺寸一半；巨化期按 GiantScaleFactor 放大（怪物侧巨化冲撞接触判定用）
        ectx.batRadius = Config::GetInstance().GetBatSize() * 0.5f *
                         (IsGiantActive() ? GiantScaleFactor : 1.0f);
        std::shared_ptr<BatSprite> bsp = std::dynamic_pointer_cast<BatSprite>(bat);
        ectx.batInvincible = bsp && (bsp->GetInvincibleState() || shieldTimer > 0.0f || giantTimer > 0.0f);
        ectx.batGiant = IsGiantActive();
        ectx.score = obstacles_score;
        ectx.timeLeft = params().timed ? timeLeft : -1.f; // <0 = 非限时
        ectx.mode = mode;
        ectx.terrainY = terrain_height;
        ectx.screenW = (float)Config::GetInstance().GetScreenWidth();
        ectx.screenH = (float)Config::GetInstance().GetScreenHeight();
        // 输入意图（取自 UISystem 推送的本帧已裁决快照：悬停在控件上的右键不会下发）
        ectx.firePrimary = gameInput.PointerPressed(GLFW_MOUSE_BUTTON_RIGHT) ||
                           gameInput.Pressed(GLFW_KEY_F);
        ectx.diveRequested = diveRequested;
        ectx.batDiving = diveTimer > 0.0f;  // 俯冲急坠中（怪物本体接触判定：俯冲撞击）
        ectx.batParry = diveIframe > 0.0f;  // 俯冲无敌帧窗口（弹反判定，§4.1 三期）
        ectx.fx = this; // MainScene 实现 IStageFeedback
        enemies.Update(dt, ectx);
    }

    CheckCollectableItemCollision();

    if (collisionEnabled) {
        std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
        if (!batSprite || batSprite->GetInvincibleState()) return;
        bool hitByObstacle = CollisionDetect();
        bool hitByLightning = CheckLightningStrikeHit(); // A4-a：落雷危险窗口内的雷击判定
        if (hitByObstacle || hitByLightning) {
            if (hitByLightning) {
                SpawnFloatText("遭雷击！", bat->GetPosition() + Vec2(0.0f, 46.0f),
                               0.87f, 0.35f, 0.35f, 24.0f);
                ReportAchievement(AchievementEvent::LightningDeath, 1);
            }
            KillPlayer(hitByLightning ? "lightning" : "collision"); // 统一致死入口（含怪物复位/音效/死亡动画/结算）
        }
    }
}

void MainScene::Render(SpriteRenderer& renderer)
{
    // ===== 全局抖动（B6-b 反馈）：平移投影使所有世界 Sprite 震动（ImGui UI 不受影响） =====
    const float SW = (float)Config::GetInstance().GetScreenWidth();
    const float SH = (float)Config::GetInstance().GetScreenHeight();
    bool shaking = shakeTimer > 0.0f;
    if (shaking) {
        float k = shakeTimer / ShakeDuration; // 1->0 衰减
        float amp = shakeIntensity * k * k;
        float offX = amp * std::sin(shakeClock * 71.0f);
        float offY = amp * std::cos(shakeClock * 53.0f);
        float proj[16];
        Ortho(offX, SW + offX, offY, SH + offY, proj);
        renderer.SetProjection(proj);
    }

    // ===== 雨：按深度分四层穿插场景（远景淡而慢、近景实而快，营造纵深） =====
    rainRenderer.RenderDepthLayer(renderer, 0); // 层0 最底层：垫在背景之下
    renderer.Draw_UV(backgroundSprites);
    rainRenderer.RenderDepthLayer(renderer, 1); // 层1 第一层远景：背景与前景之间
    renderer.Draw_UV(foregroundSprites);
    renderer.Draw_UV(terrainSprites);
    rainRenderer.RenderDepthLayer(renderer, 2); // 层2 第二层远景：地面之上、障碍之下（含贴地水花）
    if (isObstacleVisible) { // 只有当障碍物可见时才渲染它们
        renderer.Draw_UV(obstaclesUpPool);
        renderer.Draw_UV(obstaclesDownPool);
    }
    RenderItems(renderer);
    if (giantOrb && orbUsed) {
        renderer.Draw_UV(giantOrb); // 巨化星（B6-b）
    }
    // 怪物世界层（枭王：血月/本体/羽刃/水晶/音爆）——道具之后、风线之前（导演固定槽位）
    enemies.RenderWorld(renderer);
    // 风线（A4-b 补充）：插在道具与蝙蝠之间——气流贴玩法平面流动，
    // 蝙蝠保持清晰；粗实卷曲蓝线与近景雨层（层3，蝙蝠之后再画）形状迥异不混淆
    windRenderer.Render(renderer);
    renderer.Draw_UV(bat);
    // 天色变暗遮罩（A4-c）：覆盖世界元素，位于雨/闪电与 UI 之下
    if (stormScrimSprite && stormScrimAlpha > 0.001f) {
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, stormScrimAlpha);
        renderer.Draw_UV(stormScrimSprite);
    }
    rainRenderer.RenderDepthLayer(renderer, 3); // 层3 近景：障碍与蝙蝠之前（最实）
    lightningRenderer.Render(renderer);

    // 恢复未抖动的投影（其它场景共用同一渲染器）
    if (shaking) {
        float proj[16];
        Ortho(0.0f, SW, 0.0f, SH, proj);
        renderer.SetProjection(proj);
    }

    achievementToast->Render(renderer); // 成就解锁提示（B7）

    DrawImGUILayout(); 
    if (!gameOver) {
        mainSceneUILayer->Render(renderer);        
    }
    mainSceneOverLayer->Render(renderer);        
}

void MainScene::Reset()
{
    // 难度/连击/飘字/阵风/入场状态先归零，确保下方初始布局使用第 0 档参数
    obstacles_score = 0;
    lastTier = 0;
    comboCount = 0;
    comboTimer = 0.0f;
    floatingTexts.clear();
    gustCooldown = GustIntervalMin;
    gustTimer = 0.0f;
    gustForce = Vec2{0.0f, 0.0f};
    gustWarnTimer = 0.0f;
    gustPendingDuration = 0.0f;
    weatherMsgs.clear();   // 天气事件消息盒（阵风/雷击/风暴消息随局清空）
    weatherMsgClock = 0.0f;
    obstacleOscillTime = 0.0f;
    introPhase = IntroPhase::None; // 具体相位由随后的 OnEnter 决定（遮罩/直接倒数）
    introTimer = 0.0f;
    countdownValue = 3;
    giantTimer = 0.0f;
    orbUsed = false;
    orbCooldown = 20.0f;
    orbBobTimer = 0.0f;
    runSmashCount = 0;
    runNearMissCount = 0;
    shakeTimer = 0.0f;
    shakeIntensity = 0.0f;
    shakeClock = 0.0f;
    diveTimer = 0.0f;
    diveCooldown = 0.0f;
    timeLeft = params().timeLimit;
    enemies.OnRunStart(); // 怪物完全复位（须在下方障碍布局前，避免段落期残留规则抑制道具生成）
    for (auto& fly : flyingStates) fly = FlyingState{};
    for (size_t i = 0; i < obstaclesUpPool.size(); ++i) {
        obstaclesUpPool[i]->SetRotation(Vec2(1.0f, 0.0f));
        obstaclesDownPool[i]->SetRotation(Vec2(1.0f, 0.0f));
        obstaclesUpPool[i]->SetAlpha(1.0f);
        obstaclesDownPool[i]->SetAlpha(1.0f);
    }
    if (giantOrb) {
        giantOrb->Reset();
        giantOrb->setActive(false);
    }

    for (int type = 0; type < ItemTypeCount; ++type) {
        ItemUsed(type)->fill(false);
    }

    float WIN_WIDTH = Config::GetInstance().GetScreenWidth();
    float WIN_HEIGHT = Config::GetInstance().GetScreenHeight();

    // reset bg position
    float backgroundSpriteHeight = WIN_HEIGHT - terrain_height;
    for (int i = 0; i < 3; i++)
    {
        backgroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);;
        foregroundSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, backgroundSpriteHeight * 0.5f + terrain_height);
        terrainSprites[i]->SetPosition(270.0f * 0.5f + i * 270.0f, terrain_render_height * 0.5f);
    }

    // reset obstacles' position
    const float GAP_SIZE = GetCurrentGapSize(); // 难度递进：初始为第 0 档间隙
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();

    float obs_start_x = WIN_WIDTH + Config::GetInstance().GetObstacleWidth() / 2; // 从屏幕右边开始生成第一个障碍物
    float gap_center = (WIN_HEIGHT - terrain_height) * 0.5f + terrain_height; // 初始gap中心固定为正中心

    std::cout << "up pool size: " << obstaclesUpPool.size() << "\n down pool size: " << obstaclesDownPool.size() << "\n";
    float prevColumnX = -1.0f; // 上一列障碍物中心（用于在列间走廊生成道具）
    for (size_t i = 0; i < obstaclesUpPool.size() && i < obstacleStates.size(); ++i) {
        // 抽取变体（A5；重开后 tier=0，只出普通型）
        auto& st = obstacleStates[i];
        st.type = RollObstacleVariant();
        st.gapSize = (st.type == 2) ? NarrowGapOf(GAP_SIZE) : GAP_SIZE;

        // 按该障碍物自身的间隙限制中心位置
        float max_gap_center = WIN_HEIGHT - SAFE_MARGIN - st.gapSize * 0.5f;
        float min_gap_center = SAFE_MARGIN + st.gapSize * 0.5f + terrain_height;
        gap_center = std::clamp(gap_center, min_gap_center, max_gap_center);

        st.baseGapCenter = gap_center;
        st.oscillAmp = 0.0f;
        st.oscillPhase = 0.0f;
        st.nearMissChecked = false;
        ApplyObstacleGeometry(i, obs_start_x, gap_center, st.gapSize);
        SetObstacleTint(i, st.type);

        // 障碍间走廊随机生成道具（跳过第一列之前的区域）
        if (prevColumnX > 0.0f) {
            TrySpawnCorridorItem(prevColumnX, obs_start_x);
        }
        prevColumnX = obs_start_x;

        float spacing = Config::GetInstance().GetSpacingRange().first + Random::Get().GetRandomFloat() * (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
        obs_start_x += spacing;
        float delta = Config::GetInstance().GetGapCenterDeltaRange().first + Random::Get().GetRandomFloat() * (Config::GetInstance().GetGapCenterDeltaRange().second - Config::GetInstance().GetGapCenterDeltaRange().first);
        gap_center += delta;
    }
    lastSpawnedGapCenter = gap_center; // 供飞行回收重插入时衔接（B6-b）

    // reset bat position
    // 与 ReadyScene 的待机蝙蝠同位（270, 580）：转场遮罩以蝙蝠为圆心聚焦，
    // 两场景蝙蝠位置/大小一致，营造"同一只蝙蝠"的连续错觉
    bat->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.0f);
    std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
    float batSize = Config::GetInstance().GetBatSize();
    bat->SetScale(batSize, batSize); // 巨化中重开时恢复正常尺寸
    batSprite->ResetState();

    // reset variables（obstacles_score 已在函数开头归零）
    collisionEnabled = true;
    isObstacleVisible = true;
    gameOver = false;
    scoreSavedThisRun = false; // 新一局，重新允许保存分数
    energy = 0.0f;
    shieldTimer = 0.0f;
    uiPulseTimer = 0.0f;
    rainSystem->Reset();
    ApplyWeatherForTier(GetCurrentDifficultyTier()); // 天气回到当前档位基线（A4-c）

    for (int type = 0; type < ItemTypeCount; ++type) {
        for (auto& sp : *ItemPool(type)) sp->Reset();
    }

    mainSceneUILayer->Reset();
    mainSceneOverLayer->Reset();
    UISystem::Get().ClearWidgetState(hudGroupId); // 能量图标残留高亮复位
}

void MainScene::UpdateBat(float dt, bool diveRequested) {
    std::shared_ptr<BatSprite> batSprite = std::static_pointer_cast<BatSprite>(bat);

    // 俯冲冷却随世界更新推进（死亡/受击不冻结；暂停/入场由上方门控自然冻结）
    if (diveCooldown > 0.0f) diveCooldown = std::max(0.0f, diveCooldown - dt);

    if (!batSprite->GetHitState()) {
        // ===== 俯冲（B6-a）：S/↓ 触发定速急坠 0.18s（覆盖重力），全程无敌帧，冷却 2.5s =====
        if (diveTimer <= 0.0f && diveRequested && diveCooldown <= 0.0f && !IsGiantActive()) {
            diveTimer = DiveDuration;
            diveIframe = DiveInvulnDur;
            diveCooldown = DiveCooldownTime;
            if (!batSprite->GetInvincibleState())
                TryReleaseShield(DiveInvulnDur); // 无敌帧经统一入口授予（免费语义；已有护盾/无敌则不覆盖）
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Fly), 0.6f);
        }
        if (diveIframe > 0.0f) diveIframe -= dt;
        if (diveTimer > 0.0f) {
            diveTimer -= dt;
            batSprite->SetVelocity(-DiveSpeed); // 定速急坠（GL y 向上为正；覆盖重力与振翅）
            batSprite->Falling(dt);             // 复用位置积分与窗口边界钳制（贴地俯冲被钳在地面线上）
        } else {
            batSprite->Falling(dt);
            // 鼠标左键 / 键盘（空格・W・↑）：均取已裁决快照，落在控件上的点击不会下发到玩法
            bool flapByMouse = gameInput.PointerPressed(GLFW_MOUSE_BUTTON_LEFT);
            bool flapByKeyboard = gameInput.Pressed(GLFW_KEY_SPACE) ||
                                  gameInput.Pressed(GLFW_KEY_W) ||
                                  gameInput.Pressed(GLFW_KEY_UP);
            if (flapByMouse || flapByKeyboard) {
                batSprite->JumpNow();
                AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Fly), 0.8f);
            }
        }
        // A/D 或 ←/→ 持续按住 -> 横向移动（躲避全高雷柱；注意用 Held 持续语义）
        float hDir = 0.0f;
        if (gameInput.Held(GLFW_KEY_A) || gameInput.Held(GLFW_KEY_LEFT)) hDir -= 1.0f;
        if (gameInput.Held(GLFW_KEY_D) || gameInput.Held(GLFW_KEY_RIGHT)) hDir += 1.0f;
        batSprite->SetHorizontalSpeed(hDir * BatSprite::HorizontalMoveSpeed);
    }
    batSprite->Update(dt);
}

// 局内 HUD 与顶部按钮排的交互闸门（结算面板组不受影响，由 OverLayer 自管）
void MainScene::SetHudInteractive(bool interactive) {
    UISystem::Get().SetGroupEnabled(hudGroupId, interactive);
    std::static_pointer_cast<MainSceneUILayer>(mainSceneUILayer)->SetInteractive(interactive);
}

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
// 天气消息盒顶 y：贴着 SCORE 栏下缘（+2px 间距）
float WeatherBoxTopY() {
    return ScorePanelCy() + HudScoreH * 0.5f + 2.0f;
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

void MainScene::GetEnergyIconRect(float& cx, float& cy, float& size) const {
    // 布局：窗口底部中央并排一行，能量组（图标+槽）在左、俯冲长条在右，整组水平居中；
    // 绘制与点击判定共用此几何
    size = HudIconSize;
    cx = (float)Config::GetInstance().GetScreenWidth() * 0.5f
         - HudBottomGroupW * 0.5f + size * 0.5f;
    cy = BottomRowCy();
}

bool MainScene::GenerateTargetItemAt(int itemType, Vec2 position) {
    if (itemType < 0 || itemType >= ItemTypeCount) return false;
    auto& pool = *ItemPool(itemType);
    auto& used = *ItemUsed(itemType);
    for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
        if (used[i]) continue;
        pool[i]->SetPosition(position.x, position.y);
        used[i] = true;
        return true;
    }
    return false; // 该类型对象池已满
}

void MainScene::GenItem(Vec2 position) {
    float indicator = Random::Get().GetRandomFloat();
    // 高难度阶段提升能量道具占比（A4-c 风险对冲）：kiwi 10%->15%，蚊子 50%->45%
    bool highTier = GetCurrentDifficultyTier() >= 5;
    float kiwiThreshold = highTier ? 0.85f : 0.90f;
    float fireflyThreshold = highTier ? 0.45f : 0.50f;
    int selectedItemType = -1;
    if (indicator > kiwiThreshold) {
        selectedItemType = 2; // kiwi
        bool genKiwiSuccess = GenerateTargetItemAt(selectedItemType, position);
        if (!genKiwiSuccess) {
            bool genFireflySuccess = GenerateTargetItemAt(selectedItemType - 1, position);
            if (!genFireflySuccess) GenerateTargetItemAt(selectedItemType - 2, position);
        }
    }
    else if (indicator > fireflyThreshold) {
        selectedItemType = 1; // firefly
        bool genFireflySuccess = GenerateTargetItemAt(selectedItemType, position);
        if (!genFireflySuccess) GenerateTargetItemAt(selectedItemType - 1, position);
    }
    else {
        selectedItemType = 0; // mosquitoe
        GenerateTargetItemAt(selectedItemType, position);
    }
}

// 在两列障碍物之间的走廊内随机生成道具：
// 水平方向避开两端障碍物（各留半宽+余量），垂直方向限制在安全空域内随机；
// 按概率降频（穿过障碍物本身已得分，道具不再作为过关引导）。
void MainScene::TrySpawnCorridorItem(float leftColumnX, float rightColumnX) {
    // 段落期（怪物接管）：停止走廊道具生成（怪物战期间玩家专注攻击/闪避；怪物道具接管补给）
    if (enemies.Rules().suppressItemSpawn) return;

    // 巨化星（B6-b 最稀有道具）：低概率 + 冷却 + 同屏至多一颗
    if (giantOrb && !orbUsed && !IsGiantActive() && orbCooldown <= 0.0f &&
        Random::Get().GetRandomFloat() < 0.10f) {
        float minXo = leftColumnX + ItemCorridorEdgeMargin;
        float maxXo = rightColumnX - ItemCorridorEdgeMargin;
        if (maxXo - minXo > ItemMinCorridorWidth || true) {
            float x = minXo + (maxXo - minXo) * Random::Get().GetRandomFloat();
            float minY = terrain_height + ItemSpawnMinHeight;
            float maxY = (float)Config::GetInstance().GetScreenHeight() - ItemSpawnTopMargin;
            float y = minY + (maxY - minY) * Random::Get().GetRandomFloat();
            giantOrb->SetPosition(x, y);
            giantOrb->setActive(true);
            orbUsed = true;
            orbCooldown = 20.0f; // 生成冷却，控制稀有度
            std::cout << "Giant star orb spawned." << std::endl;
            return;
        }
    }

    if (Random::Get().GetRandomFloat() > ItemSpawnProbability) return;

    float minX = leftColumnX + ItemCorridorEdgeMargin;
    float maxX = rightColumnX - ItemCorridorEdgeMargin;
    if (maxX - minX < ItemMinCorridorWidth) return; // 走廊太窄，放弃生成

    float x = minX + (maxX - minX) * Random::Get().GetRandomFloat();
    float minY = terrain_height + ItemSpawnMinHeight;
    float maxY = (float)Config::GetInstance().GetScreenHeight() - ItemSpawnTopMargin;
    float y = minY + (maxY - minY) * Random::Get().GetRandomFloat();

    GenItem(Vec2{x, y});
}

void MainScene::LoadSpritesFromResourceManager() {
    // 初始化UV坐标，用于碰撞检测
    obstacleUpCollisionUVcords = ResourceManager::Get().GetUVcords3("obstacleUp");
    obstacleDownCollisionUVcords = ResourceManager::Get().GetUVcords3("obstacleDown");
    batCollisionUVcords = ResourceManager::Get().GetUVcords2("bat");

    backgroundSprites = ResourceManager::Get().GetSpriteList("background");
    foregroundSprites = ResourceManager::Get().GetSpriteList("foreground");
    terrainSprites = ResourceManager::Get().GetSpriteList("terrain");
    obstaclesUpPool = ResourceManager::Get().GetSpriteList("obstaclesUpPool");
    obstaclesDownPool = ResourceManager::Get().GetSpriteList("obstaclesDownPool");

    rainSystem = ResourceManager::Get().GetRainSystem();
    bat = ResourceManager::Get().GetSprite("bat");

    kiwis = ResourceManager::Get().GetSpriteList("kiwiPool");
    mosquitoes = ResourceManager::Get().GetSpriteList("mosquitoePool");
    fireflies = ResourceManager::Get().GetSpriteList("fireflyPool");
}

bool MainScene::CollisionDetect()
{
    if (!bat) return false;
    if (!enemies.Rules().pipesLethal || !enemies.Rules().edgesLethal)
        return false; // 段落期（怪物接管）：管道/边缘不即死，伤害走怪物专属受击模型
    if (bat->GetPosition().y - BatSprite::EdgeContactMargin <= terrain_height) {
        return true; // 碰到地面
    }
    if (bat->GetPosition().y + BatSprite::EdgeContactMargin >= Config::GetInstance().GetScreenHeight()) {
        return true; // 碰到天花板
    }

    size_t num = obstaclesUpPool.size();
    std::vector<SpriteGeometry> obstaclesUpSpritesGeometry(num);
    std::vector<SpriteGeometry> obstaclesDownSpritesGeometry(num);

    for (size_t i = 0;i < num; ++i) {
        // 飞行中的管道不参与碰撞判定（B6-b：已被撞飞）；未飞行的照常判定
        // （单管被撞飞时，留下的那根仍是真实障碍）
        bool upFlying = i < flyingStates.size() && flyingStates[i].up.flying;
        bool downFlying = i < flyingStates.size() && flyingStates[i].down.flying;
        if (upFlying) {
            obstaclesUpSpritesGeometry[i] = {Vec2(0.0f, 0.0f), Vec2(-10000.0f, -10000.0f)};
        } else {
            obstaclesUpSpritesGeometry[i] = {obstaclesUpPool[i]->GetScale(), obstaclesUpPool[i]->GetPosition()};
        }
        if (downFlying) {
            obstaclesDownSpritesGeometry[i] = {Vec2(0.0f, 0.0f), Vec2(-10000.0f, -10000.0f)};
        } else {
            obstaclesDownSpritesGeometry[i] = {obstaclesDownPool[i]->GetScale(), obstaclesDownPool[i]->GetPosition()};
        }
    }

    SpriteGeometry batSpriteGeomerty = {bat->GetScale(), bat->GetPosition()};

    return CheckCollision(obstaclesUpSpritesGeometry, batSpriteGeomerty, obstacleUpCollisionUVcords, batCollisionUVcords) || CheckCollision(obstaclesDownSpritesGeometry, batSpriteGeomerty, obstacleDownCollisionUVcords, batCollisionUVcords);   
}

void MainScene::CheckCollectableItemCollision() {
    const Vec2& batPosition = bat->GetPosition();
    const Vec2& batScale = bat->GetScale();

    // 三类道具统一轮询（碰撞命中时由 CollectableSprite 内部触发拾取回调）
    for (int type = 0; type < ItemTypeCount; ++type) {
        auto& pool = *ItemPool(type);
        auto& used = *ItemUsed(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (!used[i]) continue;
            std::static_pointer_cast<CollectableSprite>(pool[i])->CheckCollision(batPosition, batScale);
        }
    }
}

void MainScene::UpdateBackground(float dt)
{
    const float backgroundSpeed = Config::GetInstance().GetBackgroundSpeed();
    for (auto& bg : backgroundSprites)
    {
        auto pos = bg->GetPosition();
        pos.x -= backgroundSpeed * dt;

        // 如果完全移出左侧，移动到最右边
        if (pos.x <= -270.0f * 0.5f)
            pos.x += 270.0f * 3; // 3块背景拼接

        bg->SetPosition(pos.x, pos.y);
    }
}

void MainScene::UpdateForeground(float dt)
{
    const float foregroundSpeed = Config::GetInstance().GetForegroundSpeed();
    for (auto& fg : foregroundSprites)
    {
        auto pos = fg->GetPosition();
        pos.x -= foregroundSpeed * dt;

        // 如果完全移出左侧，移动到最右边
        if (pos.x <= -270.0f * 0.5f)
            pos.x += 270.0f * 3; // 3块前景拼接

        fg->SetPosition(pos.x, pos.y);
    }
}

void MainScene::UpdateTerrain(float dt)
{
    const float terrainSpeed = GetCurrentObstacleSpeed(); // 地面移动速度 == 障碍物速度（随难度递进）
    for (auto& fg : terrainSprites)
    {
        auto pos = fg->GetPosition();
        pos.x -= terrainSpeed * dt;

        // 如果完全移出左侧，移动到最右边
        if (pos.x <= -270.0f * 0.5f)
            pos.x += 270.0 * 3; // 3块地面拼接

        fg->SetPosition(pos.x, pos.y);
    }
}

void MainScene::UpdateObstacles(float dt)
{
    const float SCREEN_WIDTH  = Config::GetInstance().GetScreenWidth();
    const float SCREEN_HEIGHT = Config::GetInstance().GetScreenHeight();

    const float OBS_WIDTH = Config::GetInstance().GetObstacleWidth(); // 障碍物宽度
    const float HALF_WIDTH = OBS_WIDTH * 0.5f;

    // GAP_SIZE 的计算与变体抽取在 ReinsertColumn 中统一处理
    // SAFE_MARGIN 是为了避免 gap 生成得太靠近屏幕边缘，导致玩家没有足够反应时间
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();

    float move = GetCurrentObstacleSpeed() * dt;
    obstacleOscillTime += dt; // 摆动变体全局时钟（A5）

    // ===== 1. 向左移动 =====
    for (size_t i = 0; i < obstaclesUpPool.size() && i < obstacleStates.size(); ++i)
    {
        const bool upFlying = i < flyingStates.size() && flyingStates[i].up.flying;
        const bool downFlying = i < flyingStates.size() && flyingStates[i].down.flying;
        if (upFlying && downFlying) continue; // 整列飞行：完全交给 UpdateFlyingObstacles

        auto up   = obstaclesUpPool[i];
        auto down = obstaclesDownPool[i];
        auto& st = obstacleStates[i];

        // 列锚点：整列完好取上管；单管被撞飞时取在列的那根
        float colX = upFlying ? down->GetPosition().x : up->GetPosition().x;

        // 段落期（怪物接管）：尚未入画的列冻结在屏外，不再从右缘滚入；
        // 已入画（左缘越过屏幕右缘）的残管照常滚动自然离场
        if (enemies.Rules().freezeOffscreenPipes && colX - HALF_WIDTH >= SCREEN_WIDTH) {
            continue;
        }

        float newX = colX - move;
        const bool pairWhole = !upFlying && !downFlying; // 残缺列不计分/贴脸/摆动

        // 计分：越过屏幕中心线 +1（锚点固定，不受蝙蝠横移影响）
        if (pairWhole && up->GetPosition().x >= SCREEN_WIDTH * 0.5f && newX < SCREEN_WIDTH * 0.5f) {
            AddScore(1); // 统一入口：含模式倍率与成就上报
            ReportAchievement(AchievementEvent::PipePassed, 1);
            // 穿过障碍物得分音效
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Point), 0.9f);
        }

        // ===== 贴脸判定（A3）：以蝙蝠当前 x 为锚点（蝙蝠可横移，不再恒在屏幕中心） =====
        // 每对障碍物只判定一次，防止蝙蝠左右往返造成"反复越过"重复触发
        float batX = bat->GetPosition().x;
        if (pairWhole && !st.nearMissChecked && up->GetPosition().x >= batX && newX < batX) {
            st.nearMissChecked = true;
            const Vec2& batPos = bat->GetPosition();
            const Vec2& batScale = bat->GetScale();
            float gapLowerEdge = down->GetPosition().y + down->GetScale().y * 0.5f; // 下管道顶 = 间隙下沿
            float gapUpperEdge = up->GetPosition().y - up->GetScale().y * 0.5f;     // 上管道底 = 间隙上沿
            float clearanceLower = (batPos.y - batScale.y * 0.5f) - gapLowerEdge;   // 蝙蝠下缘与下管道的距离
            float clearanceUpper = gapUpperEdge - (batPos.y + batScale.y * 0.5f);   // 蝙蝠上缘与上管道的距离
            float minClearance = std::min(clearanceLower, clearanceUpper);
            if (minClearance >= 0.0f && minClearance < NearMissThreshold) {
                AddScore(NearMissBonus);
                runNearMissCount++;
                ReportAchievement(AchievementEvent::NearMissRun, runNearMissCount);
                SpawnFloatText("贴脸 +" + std::to_string(NearMissBonus),
                               batPos + Vec2(0.0f, 40.0f),
                               UITheme::Moonlight.r, UITheme::Moonlight.g, UITheme::Moonlight.b, 22.0f);
            }
        }

        // 滚动未飞行的管道（飞行中的由 UpdateFlyingObstacles 接管）
        if (!upFlying) up->SetPosition(newX, up->GetPosition().y);
        if (!downFlying) down->SetPosition(newX, down->GetPosition().y);

        // 摆动变体（A5）：间隙中心绕基准值正弦摆动
        // （整列完好时才摆动；残缺列保持静止几何，搭档贴回后下一帧恢复同步）
        if (pairWhole && st.type == 1 && st.oscillAmp > 0.0f) {
            float center = st.baseGapCenter + st.oscillAmp * std::sin(obstacleOscillTime * OscillSpeed + st.oscillPhase);
            float minC = SAFE_MARGIN + st.gapSize * 0.5f + terrain_height;
            float maxC = SCREEN_HEIGHT - SAFE_MARGIN - st.gapSize * 0.5f;
            center = std::clamp(center, minC, maxC);
            ApplyObstacleGeometry(i, newX, center, st.gapSize);
        }
    }

    // ===== 2. recycle（按实际位置求最左/最右「未飞行」管道；飞行回收会打破计数器顺序） =====
    size_t leftmostIdx = SIZE_MAX;   // 最左未飞行管道所属列
    bool leftmostIsUp = false;       // 该管道是上管还是下管
    float minX = 1e9f;
    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        if (!flyingStates[i].up.flying) {
            float x = obstaclesUpPool[i]->GetPosition().x;
            if (x < minX) { minX = x; leftmostIdx = i; leftmostIsUp = true; }
        }
        if (!flyingStates[i].down.flying) {
            float x = obstaclesDownPool[i]->GetPosition().x;
            if (x < minX) { minX = x; leftmostIdx = i; leftmostIsUp = false; }
        }
    }

    // 段落期（怪物接管）：停止新管道生成，让场上管道自然滚出（玩家视野聚焦怪物战）
    if (enemies.Rules().suppressPipeSpawn) return;

    if (leftmostIdx != SIZE_MAX && minX < -(HALF_WIDTH + 0.00001f))
    {
        float rightMostX = RightmostLivePipeX(leftmostIdx); // 排除本列（含其飞行中搭档）
        float spacing = Config::GetInstance().GetSpacingRange().first + Random::Get().GetRandomFloat() * (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
        float newX = (rightMostX < -9e8f)
            ? SCREEN_WIDTH + 300.0f // 无其它存活列：退到屏幕外右侧
            : rightMostX + spacing;

        // 残缺列（搭档在飞）：只重插在列的那根，搭档稍后贴回；整列完好：整列重插
        const bool partnerFlying = leftmostIsUp ? flyingStates[leftmostIdx].down.flying
                                                : flyingStates[leftmostIdx].up.flying;
        if (partnerFlying) {
            ReinsertFlyingPipe(leftmostIdx, leftmostIsUp, newX);
        } else {
            ReinsertColumn(leftmostIdx, newX);
        }

        // 在「原最右列 -> 新列」之间的走廊随机生成道具
        if (rightMostX > -9e8f) {
            TrySpawnCorridorItem(rightMostX, newX);
        }
    }
}

void MainScene::UpdateItems(float dt) {
    float move = GetCurrentObstacleSpeed() * dt; // 与障碍物同速，保持相对位置
    const int leftBorder = -25;

    // 三类道具统一处理：随世界左移 -> 动画推进 -> 出界释放回对象池
    for (int type = 0; type < ItemTypeCount; ++type) {
        auto& pool = *ItemPool(type);
        auto& used = *ItemUsed(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (!used[i]) continue;
            const auto& pos = pool[i]->GetPosition();
            pool[i]->SetPosition(pos.x - move, pos.y);
            pool[i]->Update(dt);
            if (pool[i]->GetPosition().x < leftBorder) {
                used[i] = false;
                pool[i]->Reset();
            }
        }
    }
}

void MainScene::RenderItems(SpriteRenderer& renderer) {
    for (int type = 0; type < ItemTypeCount; ++type) {
        auto& pool = *ItemPool(type);
        auto& used = *ItemUsed(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (used[i]) renderer.Draw_UV(pool[i]);
        }
    }
}

void MainScene::DrawImGUILayout() {
    if (gameOver) return; // 死亡时不绘制调试 HUD

    // 转场遮罩期间：只保留中央圆形视野（蝙蝠），主界面 UI 尚未弹出
    if (introPhase == IntroPhase::Mask) {
        TransitionMask::Get().Draw();
        return;
    }

    // ===== 常驻 HUD =====
    // SCORE：左上角紧邻帧率 overlay 下方；金币：帧率 overlay 右侧贴 FPS 框；
    // 能量组 + 俯冲长条：窗口底部中央并排一行
    DrawScoreHUD();

    DrawEnergyHUD();

    DrawCoinsHUD();

    DrawDiveHUD();

    // 天气事件消息盒（阵风/雷击/风暴档位）：贴着 SCORE 栏下缘，向下可变高
    DrawWeatherMsgBox();

    // 顶部中央：限时冲刺倒计时（B9，占据 y30），连击与其错开
    DrawModeTimerHUD();

    // 顶部中央连击提示（A3；限时模式下倒计时占用顶部，下移一行）
    DrawComboHUD();

    // 怪物前景 HUD（枭王：血条/心数/声波槽/B2 预警带/右缘渐晕）
    enemies.RenderUI();

    // 落雷预警光柱（A4-a）
    DrawLightningWarning();

    // 拾取/贴脸/跨档飘字（A2/A3）
    DrawFloatingTexts();

    // 3-2-1 倒数大字（倒数期间主界面 UI 已弹出）
    DrawIntroCountdown();

    // 转场遮罩（非激活时为空操作；正常路径此时已结束，仅兜底）
    TransitionMask::Get().Draw();

    // 调试面板（保留，但改为默认收起，减少干扰）
    if (showDebugPanel) {
        ImGui::Begin("Scene Control");
        if (ImGui::Button("Go To Ready Scene")) {
            SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
            std::cout << "Switched to ready scene..." << std::endl;
        }
        if (ImGui::Button("Reset Game")) {
            TryReleaseShield(3.0f); // 调试重开：经统一入口授予开局保护
            Reset();
        }
        ImGui::End();
    }
}

void MainScene::DrawScoreHUD() {
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
    std::string scoreStr = std::to_string(obstacles_score);
    float scoreSize = 30.0f;
    ImVec2 sv = UIBuilder::CalcTextSize(scoreStr, scoreSize);
    float scoreX = hudX + hudW * 0.5f - 16.f - sv.x;
    float scoreY = hudY + 6.f - sv.y * 0.5f;
    dl->AddText(font, scoreSize, ImVec2(scoreX + 2, scoreY + 2),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f)), scoreStr.c_str());
    dl->AddText(font, scoreSize, ImVec2(scoreX, scoreY),
                UITheme::ToImU32(UITheme::Moonlight), scoreStr.c_str());
}

void MainScene::AddEnergy(float amount) {
    if (shieldTimer > 0.0f) return; // 护盾生效期间不积攒
    // B8：充能速度升级提升所有能量获取
    energy = std::min(1.0f, energy + amount * GetChargeMultiplier());
}

void MainScene::TryReleaseShield(float freeDuration) {
    std::shared_ptr<BatSprite> batSprite = std::static_pointer_cast<BatSprite>(bat);
    if (freeDuration > 0.0f) {
        // 场景授予的免费无敌：不经能量闸门、不占护盾计时与 HUD、无护盾音效
        batSprite->SetInvincible(freeDuration);
        return;
    }
    if (gameOver || energy < 1.0f || shieldTimer > 0.0f) return;
    if (IsGiantActive()) return; // 巨化期间本身无敌，避免无敌计时被短护盾覆盖
    float dur = GetShieldDuration(); // B8：持续时间随升级等级提升
    batSprite->SetInvincible(dur);
    shieldTimer = dur;
    energy = 0.0f;
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Collect), 1.0f);
    std::cout << "Shield released! Invincible for " << dur << "s." << std::endl;
}

void MainScene::DrawEnergyHUD() {
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

    const bool isFull = energy >= 1.0f;
    const bool isActive = shieldTimer > 0.0f;
    float pulse = 0.5f + 0.5f * sin(uiPulseTimer * 6.0f); // 0~1 脉冲

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
    float frac = isActive ? shieldTimer / GetShieldDuration() : energy;
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

// ===== 护盾升级参数（B8） =====

float MainScene::GetShieldDuration() const {
    int lv = PlayerProfile::Get().GetLevel(PlayerProfile::ShieldTrack::Duration);
    return ShieldBaseDuration + ShieldDurationPerLevel * (float)lv;
}

float MainScene::GetChargeMultiplier() const {
    int lv = PlayerProfile::Get().GetLevel(PlayerProfile::ShieldTrack::Charge);
    return 1.0f + ChargeMultPerLevel * (float)lv;
}

// ===== 金币 HUD（B8）：帧率 overlay 右侧贴 FPS 框，金色胶囊 + 金币数 =====

void MainScene::DrawCoinsHUD() {
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
    std::string coinStr = std::to_string(PlayerProfile::Get().GetCoins());
    dl->AddText(font, 18.0f, ImVec2(icx + 18.0f + 1, rowY - 9.0f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), coinStr.c_str());
    dl->AddText(font, 18.0f, ImVec2(icx + 18.0f, rowY - 9.0f),
                UITheme::ToImU32(UITheme::Amber), coinStr.c_str());
}

// ===== 难度递进（A1） =====

int MainScene::GetCurrentDifficultyTier() const {
    size_t rawTier = obstacles_score / DifficultyTierSize;
    return (int)std::min(rawTier, (size_t)MaxDifficultyTier);
}

float MainScene::GetCurrentObstacleSpeed() const {
    float tier = (float)GetCurrentDifficultyTier();
    return Config::GetInstance().GetObstacleSpeed() * (1.0f + SpeedBonusPerTier * tier) * params().speedFactor;
}

float MainScene::GetCurrentGapSize() const {
    float tier = (float)GetCurrentDifficultyTier();
    return Config::GetInstance().GetGapSize() * (1.0f - GapShrinkPerTier * tier);
}

// ===== 连击（A3） =====

int MainScene::ApplyPickupComboBonus() {
    comboCount += 1;
    comboTimer = ComboWindow;
    ReportAchievement(AchievementEvent::Combo, comboCount);
    int bonus = std::min(comboCount - 1, ComboMaxBonus);
    if (bonus > 0) AddScore((size_t)bonus); // 含模式倍率与成就上报
    return bonus;
}

void MainScene::SpawnComboBonusText(int bonus) {
    if (bonus <= 0) return;
    Vec2 p = bat->GetPosition() + Vec2(0.0f, 34.0f);
    SpawnFloatText("+" + std::to_string(bonus) + " 连击x" + std::to_string(comboCount), p,
                   UITheme::Moonlight.r, UITheme::Moonlight.g, UITheme::Moonlight.b, 18.0f);
}

// ===== 飘字反馈（A2/A3） =====

void MainScene::SpawnFloatText(const std::string& text, Vec2 worldPos,
                               float r, float g, float b, float fontSize, float life) {
    floatingTexts.push_back({text, worldPos, r, g, b, fontSize, life, life});
    if (floatingTexts.size() > 16) {
        floatingTexts.erase(floatingTexts.begin()); // 防止极端情况堆积
    }
}

void MainScene::UpdateFloatingTexts(float dt) {
    for (auto& ft : floatingTexts) {
        ft.life -= dt;
        ft.worldPos.y += 55.0f * dt; // 缓慢上浮
    }
    floatingTexts.erase(
        std::remove_if(floatingTexts.begin(), floatingTexts.end(),
                       [](const FloatText& ft) { return ft.life <= 0.0f; }),
        floatingTexts.end());
}

void MainScene::DrawFloatingTexts() {
    if (floatingTexts.empty()) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    for (const auto& ft : floatingTexts) {
        float alpha = std::clamp(ft.life / ft.totalLife, 0.0f, 1.0f);
        ImVec2 ts = UIBuilder::CalcTextSize(ft.text, ft.fontSize);
        float sx = ft.worldPos.x;              // 世界 x 与屏幕 x 一致
        float sy = WIN_HEIGHT - ft.worldPos.y; // 世界 y 向上，屏幕 y 向下
        ImVec2 pos(sx - ts.x * 0.5f, sy - ts.y * 0.5f);
        dl->AddText(font, ft.fontSize, ImVec2(pos.x + 2, pos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * alpha)), ft.text.c_str());
        dl->AddText(font, ft.fontSize, pos,
                    ImGui::GetColorU32(ImVec4(ft.r, ft.g, ft.b, alpha)), ft.text.c_str());
    }
}

void MainScene::DrawComboHUD() {
    if (comboCount < 2 || comboTimer <= 0.0f) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    float frac = comboTimer / ComboWindow; // 剩余窗口比例，越少越透明
    ImU32 col = UITheme::ToImU32(UITheme::Amber, 0.35f + 0.65f * frac);
    // 非限时模式紧随金币栏（y60）；限时模式移至能量行下方（y140）避让倒计时（y 来自模式参数表）
    float y = params().comboHudY;
    UIBuilder::DrawCenteredText("连击 x" + std::to_string(comboCount),
                                WIN_WIDTH * 0.5f, y, 26.0f, col);
}

// ===== 入场流程（转场遮罩 + 3-2-1 倒数） =====

void MainScene::UpdateIntro(float dt) {
    if (introPhase == IntroPhase::Mask) {
        // 等待准备界面带来的聚焦遮罩完成揭示（保持阶段跨场景延续到主界面）
        TransitionMask::Get().Update(dt);
        if (!TransitionMask::Get().IsActive()) {
            introPhase = IntroPhase::Counting;
            countdownValue = 3;
            introTimer = 0.0f;
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Countdown), 0.5f);
        }
        return;
    }

    // Counting：每个数字停留 CountdownStepTime 秒，归零后正式开始
    introTimer += dt;
    if (introTimer >= CountdownStepTime) {
        introTimer -= CountdownStepTime;
        countdownValue -= 1;
        if (countdownValue <= 0) {
            introPhase = IntroPhase::None;
            // 正式开始：解锁操控并给予开局保护（经统一入口授予）
            TryReleaseShield(100.f);
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Start), 0.9f);
            SpawnFloatText("开始！", bat->GetPosition() + Vec2(0.0f, 60.0f),
                           UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 26.0f, 1.2f);
            std::cout << "Intro finished. Game starts." << std::endl;
        } else {
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Countdown), 0.5f); // 新数字出现时滴答
        }
    }
}

void MainScene::DrawIntroCountdown() {
    if (introPhase != IntroPhase::Counting) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    // 数字弹跳入场：前 35% 时间从 1.35 倍缩到 1.0 并淡入
    float popT = std::clamp(introTimer / (CountdownStepTime * 0.35f), 0.0f, 1.0f);
    float scale = 1.35f - 0.35f * popT;
    float alpha = 0.25f + 0.75f * popT;
    UIBuilder::DrawCenteredText(std::to_string(countdownValue),
                                WIN_WIDTH * 0.5f, 280.0f, 120.0f * scale,
                                UITheme::ToImU32(UITheme::Amber, alpha));
}

// ===== 雷击威胁（A4-a 三修：折线精确判定） =====

namespace {

// 点到线段距离（用于蝙蝠圆 vs 雷折线各段的判定）
float PointSegDistance(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float abLenSq = ab.x * ab.x + ab.y * ab.y;
    float t = 0.0f;
    if (abLenSq > 1e-8f) {
        t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / abLenSq, 0.0f, 1.0f);
    }
    float dx = p.x - (a.x + ab.x * t);
    float dy = p.y - (a.y + ab.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

// 沿折线画半透明虚线（屏幕坐标）：按弧长交替 dashOn/dashOff
void DrawDashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col,
                        float thickness, float dashOn, float dashOff) {
    bool drawing = true;
    float budget = dashOn;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        ImVec2 a = pts[i], b = pts[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        ImVec2 dir(dx / len, dy / len);
        float s = 0.0f;
        while (s < len - 1e-4f) {
            float step = std::min(budget, len - s);
            if (drawing) {
                dl->AddLine(ImVec2(a.x + dir.x * s, a.y + dir.y * s),
                            ImVec2(a.x + dir.x * (s + step), a.y + dir.y * (s + step)),
                            col, thickness);
            }
            s += step;
            budget -= step;
            if (budget <= 1e-4f) {
                drawing = !drawing;
                budget = drawing ? dashOn : dashOff;
            }
        }
    }
}

} // namespace

bool MainScene::CheckLightningStrikeHit() {
    if (!rainSystem->IsStrikeDangerous()) return false;
    // 精确判定：蝙蝠圆 vs 主分支折线。判定宽度逐段等于雷线视觉粗细
    // （含锥形收细，与 LightningRenderer 渲染公式一致），再加蝙蝠等效半径
    const Vec2 batPos = bat->GetPosition();
    const auto& pts = rainSystem->GetLightning().mainBranch.points;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float halfW = std::max(rainSystem->GetMainBranchWidthAt(i),
                               rainSystem->GetMainBranchWidthAt(i + 1)) * 0.5f; // 段内取较宽端
        float d = PointSegDistance(batPos, pts[i], pts[i + 1]);
        if (d < halfW + LightningBatRadius) return true;
    }
    return false;
}

void MainScene::DrawLightningWarning() {
    if (rainSystem->GetLightningPhase() != RainSystem::LightningPhase::Warning) return;

    const auto& pts = rainSystem->GetLightning().mainBranch.points;
    if (pts.size() < 2) return;

    const float SCREEN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float SCREEN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // 折线（世界坐标，y 向上）转屏幕坐标（y 向下）
    std::vector<ImVec2> screenPts;
    screenPts.reserve(pts.size());
    for (const auto& p : pts) {
        screenPts.emplace_back(p.x, SCREEN_HEIGHT - p.y);
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float pulse = 0.5f + 0.5f * sin(uiPulseTimer * 14.0f);
    float progress = 1.0f - rainSystem->GetWarningTimer() / rainSystem->GetLightningWarnDuration(); // 0->1 越接近落雷越醒目

    // 1) 粗范围矩形：雷可能漂移的整个区域（落点 ±漂移钳制，全高），比虚线更淡
    float sx = rainSystem->GetStrikeX();
    float zoneHalf = RainSystem::StrikeDriftClamp;
    float rectAlpha = (0.08f + 0.14f * pulse) * (0.5f + 0.5f * progress);
    dl->AddRectFilledMultiColor(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)));
    dl->AddRect(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        UITheme::ToImU32(UITheme::Amber, 0.25f + 0.35f * pulse), 4.0f, 0, 2.0f);

    // 2) 精确路径：沿主分支折线的半透明虚线（实际雷线将劈到的位置）
    float alpha = (0.30f + 0.25f * pulse) * (0.5f + 0.5f * progress);
    DrawDashedPolyline(dl, screenPts, UITheme::ToImU32(UITheme::Amber, alpha),
                       5.0f + 2.0f * progress, 16.0f, 11.0f);

    // 3) 落点标记：「雷」字（水平钳制在窗口内，避免越界）
    ImVec2 ts = UIBuilder::CalcTextSize("雷", 24.0f);
    float halfW = ts.x * 0.5f;
    float textX = std::clamp(sx, halfW + 8.0f, SCREEN_WIDTH - halfW - 8.0f);
    UIBuilder::DrawCenteredText("雷", textX, SCREEN_HEIGHT - terrain_height - 22.0f, 24.0f,
                                UITheme::ToImU32(UITheme::Amber, 0.5f + 0.5f * pulse));
}

// ===== 阵风（A4-b） =====

// 四向阵风的消息文案（按推力向量取方向：仅一轴非零）
static const char* GustWarnText(const Vec2& f) {
    if (f.y > 0.0f) return "上升气流来袭";
    if (f.y < 0.0f) return "下沉气流来袭";
    if (f.x < 0.0f) return "左向气流来袭";
    return "右向气流来袭";
}

static const char* GustStateText(const Vec2& f) {
    if (f.y > 0.0f) return "上升气流";
    if (f.y < 0.0f) return "下沉气流";
    if (f.x < 0.0f) return "左向气流";
    return "右向气流";
}

void MainScene::UpdateGust(float dt) {
    std::shared_ptr<BatSprite> batSprite = std::static_pointer_cast<BatSprite>(bat);

    // 段落期（怪物接管）：阵风暂停（§9 避免与技能双重位移压力）；
    // 进行中/预警中的阵风立即取消并移除消息行，冷却冻结
    if (enemies.Rules().pauseGust) {
        if (gustTimer > 0.0f || gustWarnTimer > 0.0f) RemoveWeatherMsg(WMsgGust);
        gustTimer = 0.0f;
        gustWarnTimer = 0.0f;
        gustForce = Vec2{0.0f, 0.0f};
        return;
    }

    // 气流生效中：纵向分量累加蝙蝠垂直速度（振翅重置=对抗）；
    // 横向分量不在此施加（输入每帧覆写横速，改为 UpdateBat 中叠加恒定漂移）
    if (gustTimer > 0.0f) {
        gustTimer = std::max(0.0f, gustTimer - dt);
        if (gustForce.y != 0.0f) {
            batSprite->SetVelocity(batSprite->GetVelocity() + gustForce.y * dt);
        }
        if (gustTimer <= 0.0f) {
            // 状态结束：移除消息行（下方消息上移补位）
            RemoveWeatherMsg(WMsgGust);
            gustForce = Vec2{0.0f, 0.0f};
        }
        return;
    }

    // 预警中（2s）：消息盒闪烁预报，尚无推力
    if (gustWarnTimer > 0.0f) {
        gustWarnTimer = std::max(0.0f, gustWarnTimer - dt);
        if (gustWarnTimer <= 0.0f) {
            // 预警结束 -> 气流生效：消息转常驻状态行
            gustTimer = gustPendingDuration;
            SetWeatherMsgSteady(WMsgGust, GustStateText(gustForce));
        }
        return;
    }

    // 冷却到点：先决定气流参数并进入预警阶段（提前 2s 闪烁预报）
    gustCooldown -= dt;
    if (gustCooldown <= 0.0f) {
        // 四向等概率抽取：0 上 / 1 下 / 2 左 / 3 右（每次至多生效一种）
        int dirIdx = (int)(Random::Get().GetRandomFloat() * 4.0f);
        float strength = 0.6f + 0.4f * Random::Get().GetRandomFloat(); // 60%~100% 强度
        // 阵风力度与间隔来自模式参数表（风暴之夜更强更频繁，B9）
        float forceMax = GustForceMax * params().gustForceFactor;
        float mag = forceMax * strength;
        switch (dirIdx) {
            case 0:  gustForce = Vec2{0.0f,  mag}; break; // 上
            case 1:  gustForce = Vec2{0.0f, -mag}; break; // 下
            case 2:  gustForce = Vec2{-mag, 0.0f}; break; // 左
            default: gustForce = Vec2{ mag, 0.0f}; break; // 右
        }
        gustPendingDuration = GustDurationMin + (GustDurationMax - GustDurationMin) * Random::Get().GetRandomFloat();
        gustCooldown = params().gustIntervalMin +
                       (params().gustIntervalMax - params().gustIntervalMin) * Random::Get().GetRandomFloat();

        PostWeatherMsg(WMsgGust, GustWarnText(gustForce));
        gustWarnTimer = WeatherWarnLead;
    }
}

// ===== 天气事件消息盒（用户需求）：左上角固定区域的事件消息流 =====

void MainScene::PostWeatherMsg(int id, const std::string& text, float flashTime, bool autoSteady) {
    // 已存在同 id：刷新内容并重新置顶；否则插入到首位（最新消息排第一行）
    for (auto it = weatherMsgs.begin(); it != weatherMsgs.end(); ++it) {
        if (it->id == id) {
            WeatherMsg m = *it;
            m.text = text;
            m.warnTimer = flashTime;
            m.steady = false;
            m.autoSteady = autoSteady;
            weatherMsgs.erase(it);
            weatherMsgs.insert(weatherMsgs.begin(), m);
            return;
        }
    }
    WeatherMsg m;
    m.id = id;
    m.text = text;
    m.warnTimer = flashTime;
    m.autoSteady = autoSteady;
    weatherMsgs.insert(weatherMsgs.begin(), m);
    if (weatherMsgs.size() > WeatherMsgCap) weatherMsgs.resize(WeatherMsgCap);
}

void MainScene::SetWeatherMsgSteady(int id, const std::string& text) {
    for (auto& m : weatherMsgs) {
        if (m.id == id) {
            m.text = text;
            m.steady = true;
            m.warnTimer = 0.0f;
            return;
        }
    }
    // 不存在（跳过了预警阶段）：直接以常驻状态行置顶加入
    WeatherMsg m;
    m.id = id;
    m.text = text;
    m.steady = true;
    weatherMsgs.insert(weatherMsgs.begin(), m);
}

void MainScene::RemoveWeatherMsg(int id) {
    for (auto it = weatherMsgs.begin(); it != weatherMsgs.end(); ++it) {
        if (it->id == id) {
            weatherMsgs.erase(it); // vector 抹除后，后续消息自动上移补位
            return;
        }
    }
}

void MainScene::UpdateWeatherFeed(float dt) {
    weatherMsgClock += dt;

    // 预警闪烁计时：autoSteady 条目到点自转常驻；其余保持闪烁直到所属系统显式转换
    for (auto& m : weatherMsgs) {
        if (!m.steady && m.warnTimer > 0.0f) {
            m.warnTimer -= dt;
            if (m.warnTimer <= 0.0f) {
                if (m.autoSteady) m.steady = true;
                else m.warnTimer = 0.0f;
            }
        }
    }

    // ===== 雷击事件（轮询 RainSystem 状态机；预警开始前 2s 闪烁预报） =====
    if (rainSystem) {
        const auto phase = rainSystem->GetLightningPhase();
        bool exists = false, steady = false;
        for (const auto& m : weatherMsgs) {
            if (m.id == WMsgLightning) { exists = true; steady = m.steady; break; }
        }

        if (phase == RainSystem::LightningPhase::None) {
            if (exists && steady) {
                RemoveWeatherMsg(WMsgLightning); // 落雷威胁解除：消息移除、下方上移
            } else if (!exists) {
                float t = rainSystem->GetTimeToNextLightning();
                if (t >= 0.0f && t <= WeatherWarnLead) {
                    PostWeatherMsg(WMsgLightning, "雷云聚集");
                }
            }
        } else {
            // 预警/落雷期间：消息转常驻「落雷预警」
            if (!steady) SetWeatherMsgSteady(WMsgLightning, "落雷预警");
        }
    }
}

void MainScene::DrawWeatherMsgBox() {
    if (weatherMsgs.empty()) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 左上角盒：贴着 SCORE 栏下缘（高度可变，向下延伸）
    const float x0 = 24.0f;
    const float top = WeatherBoxTopY();
    const float w = 224.0f;
    const float headerH = 32.0f;
    const float lineH = 30.0f;
    const float h = headerH + lineH * (float)weatherMsgs.size() + 8.0f;

    // 底板整体提高透明度（文字与状态圆点除外），减少对游戏背景的遮挡
    UIBuilder::DrawRoundedRect(x0 + w * 0.5f, top + h * 0.5f, w, h, 14.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.45f),
        UITheme::ToImU32(UITheme::AmberDim, 0.55f), 2.0f);

    // 标题
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f + 1, top + 9.0f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), "天象预警");
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f, top + 9.0f),
                UITheme::ToImU32(UITheme::AmberDim), "天象预警");

    // 消息行：最新在第一行；预警行闪烁（红点 + 琥珀文字脉冲），状态行常驻（金点 + 月光白）
    for (size_t i = 0; i < weatherMsgs.size(); ++i) {
        const auto& m = weatherMsgs[i];
        const float cy = top + headerH + lineH * (float)i + lineH * 0.5f;
        ImVec2 ts = UIBuilder::CalcTextSize(m.text, 18.0f);

        if (m.steady) {
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Amber), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Moonlight, 0.92f), m.text.c_str());
        } else {
            float pulse = 0.5f + 0.5f * std::sin(weatherMsgClock * 10.0f); // 0~1 闪烁脉冲
            float a = 0.35f + 0.65f * pulse;
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Blood, 0.4f + 0.6f * pulse), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * a)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Amber, a), m.text.c_str());
        }
    }
}

// ===== 天气联动（A4-c） =====
void MainScene::ApplyWeatherForTier(int tier) {
    const ModeParams& p = params();

    // 风暴之夜（B9）：满雨量、密集落雷、天色直接压暗（常驻值来自模式参数表，覆盖档位曲线）
    if (p.stormWeather) {
        weatherRainBaseline = p.stormRain;
        rainSystem->SetRainIntensity(p.stormRain);
        rainSystem->SetLightningIntervals(p.stormLightningMin, p.stormLightningMax);
        stormScrimTargetAlpha = p.stormScrim;
        return;
    }

    // 经典/限时：雨强/雷频/天色随难度档位渐变
    float t = std::clamp((float)tier / (float)MaxDifficultyTier, 0.0f, 1.0f);
    // 雨强 70% -> 100%（基线留档：段落期雨强下限在其上合成，结束后回落）
    weatherRainBaseline = 0.70f + 0.30f * t;
    rainSystem->SetRainIntensity(weatherRainBaseline);
    // 落雷更频繁：5~10s -> 2.5~5s
    rainSystem->SetLightningIntervals(5.0f - 2.5f * t, 10.0f - 5.0f * t);
    // 天色自第 2 档起渐深，最深 0.32
    stormScrimTargetAlpha = 0.32f * std::clamp((tier - 1) / (float)(MaxDifficultyTier - 1), 0.0f, 1.0f);
}

// ===== 障碍物变体（A5） =====
int MainScene::RollObstacleVariant() const {
    int tier = GetCurrentDifficultyTier();
    float r = Random::Get().GetRandomFloat();
    // 摆动：第 2 档起，15%+3%/档（上限 35%）；窄间隙：第 4 档起，15%+2%/档（上限 30%）
    float movingProb = (tier >= OscillatingUnlockTier) ? std::min(0.15f + 0.03f * tier, 0.35f) : 0.0f;
    float narrowProb = (tier >= NarrowUnlockTier) ? std::min(0.15f + 0.02f * tier, 0.30f) : 0.0f;
    if (r < movingProb) return 1;
    if (r < movingProb + narrowProb) return 2;
    return 0;
}

void MainScene::ApplyObstacleGeometryPipe(size_t i, bool isUp, float x, float gapCenter, float gapSize) {
    const float SCREEN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    const float OBS_WIDTH = Config::GetInstance().GetObstacleWidth();
    if (isUp) {
        float upHeight = SCREEN_HEIGHT - (gapCenter + gapSize * 0.5f);
        obstaclesUpPool[i]->SetScale(OBS_WIDTH, upHeight);
        obstaclesUpPool[i]->SetPosition(x, SCREEN_HEIGHT - upHeight * 0.5f);
    } else {
        float downHeight = gapCenter - gapSize * 0.5f - terrain_height;
        obstaclesDownPool[i]->SetScale(OBS_WIDTH, downHeight);
        obstaclesDownPool[i]->SetPosition(x, downHeight * 0.5f + terrain_height);
    }
}

void MainScene::ApplyObstacleGeometry(size_t i, float x, float gapCenter, float gapSize) {
    ApplyObstacleGeometryPipe(i, true, x, gapCenter, gapSize);
    ApplyObstacleGeometryPipe(i, false, x, gapCenter, gapSize);
}

void MainScene::SetObstacleTintPipe(size_t i, bool isUp, int type) {
    // 窄间隙管道偏红提示；其余恢复原色
    if (type == 2) {
        (isUp ? obstaclesUpPool[i] : obstaclesDownPool[i])->SetColor(1.0f, 0.76f, 0.76f, 1.0f);
    } else {
        (isUp ? obstaclesUpPool[i] : obstaclesDownPool[i])->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void MainScene::SetObstacleTint(size_t i, int type) {
    SetObstacleTintPipe(i, true, type);
    SetObstacleTintPipe(i, false, type);
}

// ===== 多模式（B9）：模式差异的唯一事实来源 =====

const MainScene::ModeParams& MainScene::ParamsOf(GameMode m) {
    static const ModeParams table[] = {
        //                name        speed score scoreEvent         gustMin gustMax gustF storm stormRain stormLMin stormLMax stormScrim timed timeLimit        comboY  lightningWarn
        // comboY（连击 HUD y）：非限时模式紧随金币栏（60）；限时模式移至能量行下方（140）避让倒计时
        /* Classic    */ { "经典无尽", 1.0f,    1, nullptr,             7.0f,  13.0f, 1.0f, false, 0.0f,     0.0f,     0.0f,     0.0f,     false, TimeRushDuration, 60.0f,  1.6f },
        /* TimeRush   */ { "限时冲刺", 1.3f,    1, "time_rush_score",   7.0f,  13.0f, 1.0f, false, 0.0f,     0.0f,     0.0f,     0.0f,     true,  TimeRushDuration, 140.0f, 1.3f },
        /* StormNight */ { "风暴之夜", 1.6f,    2, "storm_score",       3.0f,   6.0f, 1.3f, true,  1.0f,     1.8f,     3.0f,     0.32f,    false, TimeRushDuration, 60.0f,  1.0f },
    };
    return table[(int)m];
}

// ===== 道具效果表（A2 激活 + B8 货币）：蚊子/萤火虫/猕猴桃的收益与反馈 =====

const MainScene::ItemEffect& MainScene::EffectOf(int itemType) {
    static const ItemEffect table[ItemTypeCount] = {
        /* 蚊子   */ { 1, 0.0f,             1, "+1",    UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 22.0f },
        /* 萤火虫 */ { 0, EnergyPerFirefly, 1, "能量+", 0.57f,           0.85f,           0.45f,           22.0f },
        /* 猕猴桃 */ { 0, EnergyPerKiwi,    2, "能量++",0.57f,           0.85f,           0.45f,           22.0f },
    };
    return table[itemType];
}

// 道具对象池/占用标志的统一访问（下标即 ItemType）
std::vector<std::shared_ptr<Sprite>>* MainScene::ItemPool(int itemType) {
    switch (itemType) {
        case ItemMosquito: return &mosquitoes;
        case ItemFirefly:  return &fireflies;
        default:           return &kiwis;
    }
}

std::array<bool, 3>* MainScene::ItemUsed(int itemType) {
    switch (itemType) {
        case ItemMosquito: return &mosquitoesUsed;
        case ItemFirefly:  return &fireflyUsed;
        default:           return &kiwiUsed;
    }
}

void MainScene::AddScore(size_t base) {
    obstacles_score += base * (size_t)params().scoreFactor;
    ReportAchievement(AchievementEvent::RunScore, (long long)obstacles_score);
    if (mode == GameMode::TimeRush) {
        ReportAchievement(AchievementEvent::TimeRushScore, (long long)obstacles_score);
    } else if (mode == GameMode::StormNight) {
        ReportAchievement(AchievementEvent::StormScore, (long long)obstacles_score);
    }
}

void MainScene::UpdateTimeRush(float dt) {
    if (!params().timed || gameOver || introPhase != IntroPhase::None) return;
    timeLeft -= dt;
    if (timeLeft <= 0.0f) {
        timeLeft = 0.0f;
        EndRunByTimeout();
    }
}

// 局末结算（死亡动画结束 / 限时超时共用）：破纪录音效 -> 写榜 -> 金币结算 -> 结算面板
void MainScene::SettleRunResults() {
    // 破纪录判定（更新榜单前）：本局分数超过当前模式历史最高分则播放纪录音效
    {
        auto oldScores = ScoreRank::Get().GetScores(ToRankMode(mode));
        bool newRecord = oldScores.empty() ||
            obstacles_score > *std::max_element(oldScores.begin(), oldScores.end());
        if (newRecord) {
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Record), 1.0f);
        }
    }
    ScoreRank::Get().UpdateScore(obstacles_score, ToRankMode(mode)); // 写入当前模式榜单
    scoreSavedThisRun = true; // 已保存，退出场景时不再重复保存

    // 金币结算：分数/5 追加并落盘（B8）
    {
        long long bonus = (long long)(obstacles_score / 5);
        PlayerProfile::Get().AddCoins(bonus);
        PlayerProfile::Get().Save();
        if (bonus > 0) {
            const float W = (float)Config::GetInstance().GetScreenWidth();
            const float H = (float)Config::GetInstance().GetScreenHeight();
            SpawnFloatText("金币 +" + std::to_string(bonus),
                           Vec2(W * 0.5f, H - 240.0f),
                           0.95f, 0.82f, 0.35f, 24.0f, 2.0f);
        }
    }

    auto overLayer = std::static_pointer_cast<MainSceneOverLayer>(mainSceneOverLayer);
    overLayer->SetCurrentScore((int)obstacles_score);
    auto scores = ScoreRank::Get().GetScores(ToRankMode(mode)); // 当前模式最高分
    int best = scores.empty() ? (int)obstacles_score : (int)scores.front();
    overLayer->SetBestScore(best);
    overLayer->ActivateLayer();
}

void MainScene::EndRunByTimeout() {
    if (gameOver) return;
    std::cout << "Time's up! Final score: " << obstacles_score << std::endl;
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::GameOver), 1.0f);
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    SpawnFloatText("时间到！", Vec2(WIN_WIDTH * 0.5f, WIN_HEIGHT - 200.0f),
                   UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 30.0f, 2.0f);

    SettleRunResults(); // 破纪录音效/写榜/金币结算/结算面板（与死亡结算共用，无死亡动画）

    collisionEnabled = false;
    gameOver = true;
}

// ===== IStageFeedback（怪物表现服务）：MainScene 侧实现，全部为既有能力的薄转发 =====
// 契约见 docs/怪物系统架构设计.md §4.3；怪物经 ctx.fx 上行调用，场景不解释怪物事件

void MainScene::FloatTextAt(const std::string& text, Vec2 pos,
                            float r, float g, float b, float size, float life) {
    SpawnFloatText(text, pos, r, g, b, size, life); // 纯透传（颜色词汇与 SpawnFloatText 一致）
}

void MainScene::PlaySFX(int sfxId, float volume) {
    AudioSystem::Instance().PlaySFX(SFXToFile(static_cast<SFX>(sfxId)), volume);
}

void MainScene::Shake(float intensity) {
    TriggerShake(intensity);
}

// PostWeatherMsg/RemoveWeatherMsg 的接口实现即场景既有同名方法，无需另写转发

void MainScene::AddCoins(int n) {
    PlayerProfile::Get().AddCoins((long long)n);
    PlayerProfile::Get().Save(); // 与原击杀奖励路径一致：入账即落盘
}

void MainScene::AddTimeLimit(float sec) {
    if (!params().timed) return; // 非限时模式为空操作
    timeLeft = std::min(TimeRushDuration, timeLeft + sec);
}

void MainScene::ReportAchievement(AchievementEvent event, long long value) {
    Achievements::Get().Report(event, value);
}

// 统一致死入口（架构设计阶段 0）：碰撞/落雷致死与怪物致死共用同一流程，
// 原怪物致死入口与碰撞分支的平行实现由此合一
void MainScene::KillPlayer(const char* reason) {
    if (!collisionEnabled) return; // 已在死亡流程中
    collisionEnabled = false;      // 禁止后续碰撞/受击再触发
    enemies.OnBatDead();           // 怪物全员复位（段落中止，再降临计时从头开始）
    std::cout << "Killed by " << reason << ". Switching to death animation." << std::endl;
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Death), 1.0f);
    std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
    if (!batSprite) return;
    batSprite->OnBatHit(); // 死亡动画
    bat->setAnimationCallback([this]() {
        gameOver = true; // 动画结束后设置游戏结束标志
        rainSystem->SetScrollSpeed(0.0f); // 地面已静止，水花漂移与地形偏移同步冻结
        SettleRunResults(); // 破纪录音效/写榜/金币结算/结算面板（与超时结算共用）
        AudioSystem::Instance().PlaySFX(SFXToFile(SFX::GameOver), 1.0f);
        std::cout << "Death animation finished. Game Over." << std::endl;
    });
}

void MainScene::BounceBat(float dx, float dy) {
    Vec2 p = bat->GetPosition();
    p.x += dx;
    p.y += dy;
    bat->SetPosition(p.x, p.y);
    // 击退/弹起是物理积分之外的直接位移（如俯冲撞击向左弹开 160px），
    // 统一钳回窗口活动范围，防止蝙蝠被顶出屏幕出现单帧消失
    std::static_pointer_cast<BatSprite>(bat)->ConstrainWithinWindow();
}

// 终止巨化（巨化冲撞命中怪物后由怪物侧经 fx 申请；自然到期亦走此处统一还原）
void MainScene::EndGiant() {
    if (giantTimer <= 0.0f) return;
    giantTimer = 0.0f;
    auto batSprite = std::static_pointer_cast<BatSprite>(bat);
    float batSize = Config::GetInstance().GetBatSize();
    batSprite->SetScale(batSize, batSize);
    if (shieldTimer <= 0.0f)
        batSprite->SetInvincible(0.01f); // 巨化无敌随效果终止（极短计时立即回退；护盾在身时不清护盾免伤）
    SpawnFloatText("巨化结束", bat->GetPosition() + Vec2(0.0f, 70.0f),
                   0.85f, 0.85f, 0.9f, 20.0f);
}

void MainScene::OnEnemyItemCollected(Vec2 /*pos*/) {
    // 预留：怪物道具接通连击/成就通道（枭王设计文档 §4.5）；一期行为不变
}

// 蝙蝠受击无敌授予（怪物侧受击模型 §6）：闪烁反馈由 BatSprite 无敌期辉光脉动承担。
// Timer::Start 会无条件重置计时，故仅在能延长时授予——防止把更长的护盾/巨化/俯冲无敌缩短（§6 护盾照常消耗）。
void MainScene::GrantBatInvuln(float dur) {
    auto batSprite = std::static_pointer_cast<BatSprite>(bat);
    if (!batSprite || dur <= 0.0f) return;
    if (batSprite->GetInvincibleRemaining() >= dur) return;
    batSprite->SetInvincible(dur);
}

// 击杀慢动作：取较长请求即可；缩放曲线见 TimeScale
void MainScene::TriggerSlowMo(float dur) {
    slowMoTimer = std::max(slowMoTimer, dur);
}

float MainScene::TimeScale() const {
    if (slowMoTimer <= 0.0f) return 1.0f;
    const float k = std::clamp(slowMoTimer / SlowMoDur, 0.0f, 1.0f);  // 1→0（钳制防外部更长请求致负缩放）
    return 1.0f - (1.0f - SlowMoScale) * k;       // 触发瞬间 SlowMoScale，线性缓升回 1
}

// ===== 俯冲冷却 HUD（B6-a）：底部并排行右端长条（▼ 俯冲）；冷却期沿长条周长环绕描边 =====
void MainScene::DrawDiveHUD() {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 长条底板：窗口底部中央并排一行，位于能量组右侧（整组水平居中）
    const float w = DiveBarW;
    const float h = DiveBarH;
    const float cx = (float)Config::GetInstance().GetScreenWidth() * 0.5f
                     + HudBottomGroupW * 0.5f - w * 0.5f;
    const float cy = BottomRowCy();

    const bool ready = diveCooldown <= 0.0f && diveTimer <= 0.0f;
    const bool diving = diveTimer > 0.0f;
    float pulse = 0.5f + 0.5f * sin(uiPulseTimer * 6.0f);

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
    if (diveCooldown > 0.0f) {
        const float frac = diveCooldown / DiveCooldownTime;
        StrokeRoundedRectPerimeter(dl,
                                   ImVec2(cx - w * 0.5f - 2.0f, cy - h * 0.5f - 2.0f),
                                   ImVec2(cx + w * 0.5f + 2.0f, cy + h * 0.5f + 2.0f),
                                   10.0f, frac,
                                   UITheme::ToImU32({0.57f, 0.85f, 0.95f}, 0.8f), 2.5f);
    }
}

void MainScene::DrawModeTimerHUD() {
    if (!params().timed) return;
    const float WIN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", std::max(0.0f, timeLeft));
    ImU32 col = (timeLeft <= 10.0f)
        ? UITheme::ToImU32(UITheme::Blood, 0.7f + 0.3f * (0.5f + 0.5f * sin(uiPulseTimer * 10.0f)))
        : UITheme::ToImU32(UITheme::Amber);
    // 金币栏（y13~43）下方、枭王名号（y88 起）上方
    UIBuilder::DrawCenteredText(buf, WIN_WIDTH * 0.5f, 60.0f, 30.0f, col);
}

// ===== 巨型蝙蝠（B6-b） =====

void MainScene::ActivateGiantMode() {
    auto batSprite = std::static_pointer_cast<BatSprite>(bat);
    float batSize = Config::GetInstance().GetBatSize();
    batSprite->SetScale(batSize * GiantScaleFactor, batSize * GiantScaleFactor);
    TryReleaseShield(GiantDuration); // 巨化无敌经统一入口授予（免费语义：不耗能量、不占护盾 HUD）
    giantTimer = GiantDuration;
    PlayerProfile::Get().AddCoins(5); // 巨化星额外奖励金币（B8）
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Record), 1.0f); // 稀有道具登场
    SpawnFloatText("巨化！", bat->GetPosition() + Vec2(0.0f, 80.0f),
                   0.95f, 0.85f, 0.4f, 30.0f, 1.5f);
    TriggerShake(8.0f);
    std::cout << "Giant mode activated for " << GiantDuration << "s." << std::endl;
}

void MainScene::UpdateGiant(float dt) {
    if (giantTimer <= 0.0f) return;
    giantTimer -= dt;
    auto batSprite = std::static_pointer_cast<BatSprite>(bat);
    float batSize = Config::GetInstance().GetBatSize();

    if (giantTimer <= 0.0f) {
        giantTimer = 0.01f; // 保持活动语义，由 EndGiant 统一归零并还原体型/无敌
        EndGiant();
        return;
    }

    // 巨化期间保持 2 倍体型；最后 0.4s 平滑缩回
    float scale = batSize * GiantScaleFactor;
    if (giantTimer < 0.4f) {
        scale = batSize + (batSize * GiantScaleFactor - batSize) * (giantTimer / 0.4f);
    }
    batSprite->SetScale(scale, scale);

    // ===== 撞飞判定：与列横向重叠，且纵向与某根管道相交（只撞飞实际碰到的那根） =====
    const Vec2& bp = bat->GetPosition();
    const float OBS_HALF = Config::GetInstance().GetObstacleWidth() * 0.5f;
    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        const auto& fly = flyingStates[i];
        if (fly.up.flying && fly.down.flying) continue; // 整列都在飞
        // 列锚点取未飞行的那根（单管被撞飞后另一根仍在列上）
        float colX = fly.up.flying ? obstaclesDownPool[i]->GetPosition().x
                                   : obstaclesUpPool[i]->GetPosition().x;
        float dx = std::abs(bp.x - colX);
        if (dx > scale * 0.5f + OBS_HALF) continue;
        float upBottom = obstaclesUpPool[i]->GetPosition().y - obstaclesUpPool[i]->GetScale().y * 0.5f;
        float downTop = obstaclesDownPool[i]->GetPosition().y + obstaclesDownPool[i]->GetScale().y * 0.5f;
        bool hitUp = !fly.up.flying && (bp.y + scale * 0.5f > upBottom);     // 上缘探入上管
        bool hitDown = !fly.down.flying && (bp.y - scale * 0.5f < downTop);  // 下缘探入下管
        if (hitUp || hitDown) {
            SmashObstacles(i, hitUp, hitDown);
        }
    }
}

// HSV(h∈[0,1), s, v) -> RGB：巨化星炫彩流转着色用
static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    int hi = (int)(h * 6.0f);
    float f = h * 6.0f - (float)hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (hi % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

void MainScene::UpdateOrb(float dt) {
    orbCooldown = std::max(0.0f, orbCooldown - dt);
    if (!orbUsed || !giantOrb) return;

    orbBobTimer += dt;
    const auto& pos = giantOrb->GetPosition();
    giantOrb->SetPosition(pos.x - GetCurrentObstacleSpeed() * dt, pos.y);
    giantOrb->Update(dt);

    // ===== 炫彩流转（B6-b）：色相随时间循环（约 2.5s 一圈，从金色起转），星体为灰度白底，乘出全谱段彩虹 =====
    {
        float r, g, b;
        HsvToRgb(0.11f + orbBobTimer * 0.4f, 0.9f, 1.0f, r, g, b); // 0.11 = 金色起点
        giantOrb->SetColor(r, g, b, 1.0f);
    }
    giantOrb->SetGlow(0.9f + 0.7f * std::sin(orbBobTimer * 6.0f)); // 发光脉冲（与变色叠加，更抓眼）

    // 出界回收
    if (giantOrb->GetPosition().x < -40.0f) {
        orbUsed = false;
        giantOrb->setActive(false);
        return;
    }

    // 拾取判定（AABB，与 CollectableSprite 同思路；巨化中的大碰撞盒也更容易吃到）
    const Vec2& batPos = bat->GetPosition();
    const Vec2& batScale = bat->GetScale();
    const Vec2& orbScale = giantOrb->GetScale();
    const Vec2& cur = giantOrb->GetPosition();
    if (std::abs(cur.x - batPos.x) < (orbScale.x + batScale.x) * 0.5f &&
        std::abs(cur.y - batPos.y) < (orbScale.y + batScale.y) * 0.5f) {
        orbUsed = false;
        giantOrb->setActive(false);
        ActivateGiantMode();
    }
}

void MainScene::SmashObstacles(size_t i, bool smashUp, bool smashDown) {
    auto& fly = flyingStates[i];
    // 水平初速同侧共用（同一次撞飞的管道向同一方向崩开，视觉成组）
    float dirX = (Random::Get().GetRandomFloat() < 0.5f ? -1.0f : 1.0f) * (160.0f + 220.0f * Random::Get().GetRandomFloat());
    auto rollSpin = []() {
        return (Random::Get().GetRandomFloat() < 0.5f ? -1.0f : 1.0f) * (3.5f + 3.0f * Random::Get().GetRandomFloat());
    };
    if (smashUp) {
        fly.up = FlyingPipe{}; // 整体重置，清掉上一轮残留的 angle/life
        fly.up.flying = true;
        fly.up.vel = Vec2(dirX, 320.0f + 260.0f * Random::Get().GetRandomFloat()); // 上管向上抛
        fly.up.spin = rollSpin();
    }
    if (smashDown) {
        fly.down = FlyingPipe{};
        fly.down.flying = true;
        // 下管向下砸（重力会让下管更快离场），制造"轰开"的视觉
        fly.down.vel = Vec2(dirX, -(180.0f + 200.0f * Random::Get().GetRandomFloat()));
        fly.down.spin = rollSpin();
    }

    runSmashCount += (smashUp ? 1 : 0) + (smashDown ? 1 : 0); // 按根计数
    ReportAchievement(AchievementEvent::SmashRun, runSmashCount);
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Thunder), 1.0f); // 撞击音效
    TriggerShake(12.0f);
    SpawnFloatText("撞飞！", bat->GetPosition() + Vec2(0.0f, 70.0f),
                   0.95f, 0.75f, 0.4f, 24.0f);
    std::cout << "Obstacle " << i << (smashUp && smashDown ? " pair" : (smashUp ? " up pipe" : " down pipe"))
              << " smashed away!" << std::endl;
}

// 单根飞行管道：抛物线 + 自旋 + 淡出；返回是否结束（淡尽/出界/超时）
bool MainScene::UpdateFlyingPipe(FlyingPipe& fp, const std::shared_ptr<Sprite>& pipe, float dt) {
    fp.life += dt;
    fp.vel.y -= 700.0f * dt; // 抛物线重力
    fp.angle += fp.spin * dt;
    pipe->SetPosition(pipe->GetPosition().x + fp.vel.x * dt,
                      pipe->GetPosition().y + fp.vel.y * dt);
    pipe->SetRotation(Vec2(std::cos(fp.angle), std::sin(fp.angle)));
    float alpha = std::clamp(1.6f - fp.life, 0.0f, 1.0f); // 后段淡出
    pipe->SetAlpha(alpha);

    const float SCREEN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    float x = pipe->GetPosition().x;
    bool offscreen = x < -260.0f || x > SCREEN_WIDTH + 320.0f;
    return alpha <= 0.02f || offscreen || fp.life > 2.5f;
}

// 最右「未飞行」管道的 x；excludePair 对应配对整对排除（SIZE_MAX = 不排除）
float MainScene::RightmostLivePipeX(size_t excludePair) const {
    float rx = -1e9f;
    for (size_t j = 0; j < obstaclesUpPool.size() && j < flyingStates.size(); ++j) {
        if (j == excludePair) continue;
        if (!flyingStates[j].up.flying)
            rx = std::max(rx, obstaclesUpPool[j]->GetPosition().x);
        if (!flyingStates[j].down.flying)
            rx = std::max(rx, obstaclesDownPool[j]->GetPosition().x);
    }
    return rx;
}

void MainScene::UpdateFlyingObstacles(float dt) {
    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        auto& fly = flyingStates[i];
        if (!fly.up.flying && !fly.down.flying) continue;

        // 各管独立推进（只撞飞的那根在飞，另一根由 UpdateObstacles 正常滚动）
        bool upDone = fly.up.flying && UpdateFlyingPipe(fly.up, obstaclesUpPool[i], dt);
        bool downDone = fly.down.flying && UpdateFlyingPipe(fly.down, obstaclesDownPool[i], dt);
        if (!upDone && !downDone) continue;

        // 结束的管道先脱离飞行状态，再按搭档状态分流回收
        if (upDone) fly.up.flying = false;
        if (downDone) fly.down.flying = false;

        // 飞行结束且搭档仍在飞：本管先行重插为「新列」（最右存活列右侧），搭档稍后贴回；
        // 同帧双结束时上管先重插、下管随即贴回，等效于旧的整列回收
        auto recycleAsNewColumn = [this, i](bool isUp) {
            float rightMostX = RightmostLivePipeX(i); // 本对两管此刻都非「存活锚点」，天然排除
            float spacing = Config::GetInstance().GetSpacingRange().first + Random::Get().GetRandomFloat() * (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
            float newX = (rightMostX < -9e8f)
                ? (float)Config::GetInstance().GetScreenWidth() + 300.0f
                : rightMostX + spacing;
            ReinsertFlyingPipe(i, isUp, newX);
            if (rightMostX > -9e8f) {
                TrySpawnCorridorItem(rightMostX, newX);
            }
        };

        if (upDone && downDone) {
            recycleAsNewColumn(true);
            RejoinFlyingPipe(i, false); // 下管贴回刚重插的上管
        } else if (upDone) {
            if (fly.down.flying) recycleAsNewColumn(true); // 搭档还在飞：本管先重插
            else RejoinFlyingPipe(i, true);                // 搭档在列：贴回
        } else { // downDone
            if (fly.up.flying) recycleAsNewColumn(false);
            else RejoinFlyingPipe(i, false);
        }
    }
}

// 飞行结束、搭档仍在列：贴回搭档当前列（按搭档实际几何反推 gap 中心，无缝衔接）
void MainScene::RejoinFlyingPipe(size_t i, bool isUp) {
    auto pipe = isUp ? obstaclesUpPool[i] : obstaclesDownPool[i];
    auto partner = isUp ? obstaclesDownPool[i] : obstaclesUpPool[i];
    const auto& st = obstacleStates[i];

    float gapCenter;
    if (isUp) {
        // 搭档是下管：其顶面 = 间隙下沿
        gapCenter = partner->GetPosition().y + partner->GetScale().y * 0.5f + st.gapSize * 0.5f;
    } else {
        // 搭档是上管：其底面 = 间隙上沿
        gapCenter = partner->GetPosition().y - partner->GetScale().y * 0.5f - st.gapSize * 0.5f;
    }

    pipe->SetRotation(Vec2(1.0f, 0.0f));
    pipe->SetAlpha(1.0f);
    ApplyObstacleGeometryPipe(i, isUp, partner->GetPosition().x, gapCenter, st.gapSize);
}

// 飞行结束且搭档仍在飞：本管先重插为「新列」（抽取新配对状态，布置到 newX）
void MainScene::ReinsertFlyingPipe(size_t i, bool isUp, float newX) {
    RollColumnState(i);
    const auto& st = obstacleStates[i];
    auto pipe = isUp ? obstaclesUpPool[i] : obstaclesDownPool[i];
    pipe->SetRotation(Vec2(1.0f, 0.0f));
    pipe->SetAlpha(1.0f);
    ApplyObstacleGeometryPipe(i, isUp, newX, st.baseGapCenter, st.gapSize);
    SetObstacleTintPipe(i, isUp, st.type);
}

// 抽取新变体 + 平滑随机 gap：只更新 obstacleStates[i] 与 lastSpawnedGapCenter，不动管道几何
void MainScene::RollColumnState(size_t i) {
    const float SCREEN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();
    const float GAP_SIZE = GetCurrentGapSize();

    // ---- 抽取变体（A5）：普通 / 摆动 / 窄间隙 ----
    auto& st = obstacleStates[i];
    st.type = RollObstacleVariant();
    st.gapSize = (st.type == 2) ? NarrowGapOf(GAP_SIZE) : GAP_SIZE;

    // ---- 平滑随机 gap（衔接上一生成中心，按该变体间隙约束） ----
    float delta = Config::GetInstance().GetGapCenterDeltaRange().first + Random::Get().GetRandomFloat() * (Config::GetInstance().GetGapCenterDeltaRange().second - Config::GetInstance().GetGapCenterDeltaRange().first);
    lastSpawnedGapCenter += delta;

    float minCenter = SAFE_MARGIN + st.gapSize * 0.5f + terrain_height;
    float maxCenter = SCREEN_HEIGHT - SAFE_MARGIN - st.gapSize * 0.5f;
    lastSpawnedGapCenter = std::clamp(lastSpawnedGapCenter, minCenter, maxCenter);

    st.baseGapCenter = lastSpawnedGapCenter;
    // 摆幅不超过间隙一半，保证间隙在摆动过程中始终清晰可通行
    st.oscillAmp = (st.type == 1)
        ? std::clamp(25.0f + 15.0f * Random::Get().GetRandomFloat(), 10.0f, st.gapSize * 0.5f - 16.0f)
        : 0.0f;
    st.oscillPhase = Random::Get().GetRandomFloat() * 6.2831853f;
    st.nearMissChecked = false; // 新生成的障碍物重新允许贴脸判定
}

void MainScene::ReinsertColumn(size_t i, float newX) {
    RollColumnState(i);
    const auto& st = obstacleStates[i];
    ApplyObstacleGeometry(i, newX, st.baseGapCenter, st.gapSize);
    SetObstacleTint(i, st.type);
}

// ===== 全局抖动（B6-b 反馈） =====

void MainScene::TriggerShake(float intensity) {
    shakeTimer = ShakeDuration;
    shakeIntensity = intensity;
    shakeClock = 0.0f;
}

void MainScene::UpdateShake(float dt) {
    if (shakeTimer <= 0.0f) return;
    shakeTimer = std::max(0.0f, shakeTimer - dt);
    shakeClock += dt;
}

// ===== 成就解锁提示（B7） =====

void MainScene::PollAchievementToasts() {
    AchievementToast toastInfo{};
    if (!Achievements::Get().PopPendingToast(toastInfo)) return;
    const char* name = "?";
    for (const auto& def : Achievements::Defs()) {
        if (def.id == toastInfo.id) {
            name = def.name;
            break;
        }
    }
    auto toast = std::static_pointer_cast<ToastLayer>(achievementToast);
    toast->SetText("成就达成 · " + std::string(name) +
                   " Lv." + std::to_string(toastInfo.level));
    toast->SetColor(Vec3{0.941f, 0.722f, 0.314f});
    toast->Activate();
}
