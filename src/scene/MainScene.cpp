#include "app/Services.h"
#include "scene/MainScene.h"
#include "render/WorldSlot.h"
#include "render/RenderContext.h"
#include "app/Application.h"

#include "sprite/Sprite.h"
#include "sprite/BatSprite.h"
#include "sprite/CollectableSprite.h"

#include "core/Config.h"
#include "gameplay/ModeRegistry.h"

#include "res/ResourceManager.h"
#include "scene/stage/RainSystem.h" // RequestLightningStrike 转发目标（TriggerStrikeAt 完整类型）
#include "audio/AudioSystem.h"
#include <GLFW/glfw3.h>   // 仅取 GLFW_KEY_* / GLFW_MOUSE_BUTTON_* 常量（输入由 UISystem 推送）
#include "imgui.h"
#include "scene/SceneManager.h"
#include "scene/ReadyScene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>

#include "layer/MainSceneUILayer.h"
#include "layer/MainSceneOverLayer.h"
#include "layer/ToastLayer.h"
#include "ui/FontManager.h"
#include "app/ScoreRank.h"
#include "app/Achievements.h"
#include "app/PlayerProfile.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"

namespace {
// 局内 HUD 交互组层叠：低于暂停面板(40)/结算面板(50)，与顶部图标排(10) 同时参与命中
constexpr int kLayerMainHud = 12;
}

// 第 2 期：RankMode 枚举与 ToRankMode 映射已移除。
// ScoreRank 直接接收 ModeKey 并向 ModeRegistry 查注册序，新增模式无需任何映射。

MainScene::MainScene(Services& services)
    : svc_(services),
      terrain_render_height(services.Cfg().GetTerrainRenderHeight()),
      terrain_height(services.Cfg().GetTerrainHeight())
{
    LoadSpritesFromResourceManager();

    // ===== 道具/障碍子系统：拾取收益由场景统一结算 =====
    // 效果全部来自 ItemField::EffectOf 参数表（模式倍率/成就/连击在场景侧应用）
    items.SetPickupHandler([this](int type) { OnItemPicked(type); });
    items.SetGiantOrbHandler([this]() { player.ActivateGiant(); });
    obstacles.SetFeedback(this, this); // 两个窄接口：表现 + 结算
    obstacles.SetCorridorItemSpawner([this](float left, float right) {
        items.TrySpawnCorridor(left, right, BuildFrame(0.0f));
    });

    // 天气子系统：取 RainSystem、初始化雨/雷/风渲染器与天色遮罩
    weather.Init(svc_.Cfg(), svc_.Res(), svc_.Audio());

    // 玩家能力子系统：共享蝙蝠精灵，上行反馈只依赖表现接口（IFeedbackFx）
    player.Init(std::static_pointer_cast<BatSprite>(bat), this,
                svc_.Cfg(), svc_.Profile());

    // 怪物（EnemyDirector 框架）：登记内置怪物并初始化（纹理已在资源加载阶段就绪）
    RegisterBuiltinEnemies(enemies);
    enemies.InitAll(svc_.Cfg(), svc_.Res());

    mainSceneUILayer = std::make_shared<MainSceneUILayer>(svc_);
    mainSceneOverLayer = std::make_shared<MainSceneOverLayer>(svc_);

    // ===== 局内 HUD 交互控件：能量图标热区 =====
    // 命中矩形与绘制几何同源（GetEnergyIconRect）；可点与否由 SetEnabled 逐帧同步，
    // 鼠标判定不再出现在本场景代码里
    hudGroupId = svc_.UI().CreateGroup(kLayerMainHud);
    energyIcon->SetPlayFeedbackSFX(false); // 原实现无音效，保持听感不变
    energyIcon->SetOnClick([this]() { player.TryReleaseShield(); });
    svc_.UI().Add(hudGroupId, energyIcon);
    energyScope.Bind(svc_.UI(), hudGroupId, false);

    // 玩法操控的按键集：声明给 UISystem，本场景只读它推送的回帧快照
    svc_.UI().WatchKeys({GLFW_KEY_SPACE, GLFW_KEY_W, GLFW_KEY_UP,
                               GLFW_KEY_S, GLFW_KEY_DOWN,
                               GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
                               GLFW_KEY_F});

    // 成就解锁提示（底部居中，带专属面板背景；任何阶段都可能弹出）
    auto achToast = std::make_shared<ToastLayer>(2.5f, "", Vec3{0.941f, 0.722f, 0.314f}, 26.0f,
                                                 Vec2{270.0f, 838.0f});
    if (auto tex = svc_.Res().GetTexture2D("v2_ui_achievement_toast")) {
        achToast->SetBackground(tex);
    }
    achievementToast = achToast;

    // 覆盖层入栈：插入顺序即更新顺序；渲染再按 RenderPhase 分段
    overlayLayers.Add(achievementToast);
    overlayLayers.Add(mainSceneUILayer);
    overlayLayers.Add(mainSceneOverLayer);

    // 暂停面板的"重新开始"：走 3-2-1 倒数流程（无转场遮罩），倒数结束统一给予无敌
    mainSceneUILayer->SetRestartCallback([this](){
        Reset();
        OnEnter();
    });

    // GameOver 重新开始：同上，倒数后开局
    mainSceneOverLayer->SetRestartEvent([this](){
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
    UISystem& ui = svc_.UI();
    ui.AddGameInputSink(this);
    overlayLayers.AttachAll(); // UI 层订阅输入；结算/Toast 回到未激活状态

    // 进入时先收起局内交互，由 Update 按入场阶段开启
    mainSceneUILayer->SetUpdateEnabled(false);
    mainSceneUILayer->SetRenderEnabled(!flow.IsGameOver());
    SetHudInteractive(false); // 能量图标 + UI 层输入同步收起

    // ===== 局内音频：只有雨声，没有背景音乐 =====
    // 背景音乐由组合根在启动时统一起播（Application::Init），此处暂停它。
    // 用暂停而非停止：ma_sound 保留播放游标，离开局内时从原位置续播，
    // 不会因为每局进出都把曲子重头放一遍。Pause/Resume 均幂等，
    // 局内重开（Reset + OnEnter，不经 SceneManager）重复调用也安全。
    AudioSystem& audio = svc_.Audio();
    audio.PauseBGM();
    audio.PlayAmbient("assets/Audio/Sound Effects/Rain.wav", true, 0.4f);
    weather.OnEnter(params().lightningWarnLead); // 落雷预警提前量（随模式差异化）
    weather.ApplyForTier(GetCurrentDifficultyTier(), params()); // 天气基线（A4-c；场景切换前已 Reset，此处为第 0 档）

    // 固定天气模式：常驻消息行显示该模式的注册名（不再硬编码中文模式名）
    if (params().stormWeather) {
        weather.Post(WMsgStorm,
                     ModeRegistry::Instance().Get(flow.Mode()).DisplayName(),
                     0.8f, true);
    }

    // 入场流程：从准备界面"开始游戏"进入时转场仍在全屏接管（先揭示后倒数）；
    // 局内重开无转场，直接 3-2-1 倒数。开局无敌改由倒数结束时统一给予。
    // 本场景只查询 SceneManager 的接管状态，不认识任何具体转场实现
    const bool maskActive = svc_.Scenes().IsTransitionActive();
    flow.OnEnter(maskActive);
    if (!maskActive) {
        svc_.Audio().PlaySFX(SFXToFile(SFX::Countdown), 0.5f);
    }
}

void MainScene::OnPrepare(const SceneArgs& args)
{
    // 启动参数（对局模式）须在 Reset 前到位：Reset 的 flow.Reset(params())、
    // 天气基线与初始障碍布局都依赖模式参数表
    flow.SetMode(args.gameMode);
}

void MainScene::OnExit()
{
    // 局内音频收尾：停雨声，恢复菜单背景音乐（从 OnEnter 暂停的位置续播）
    AudioSystem& audio = svc_.Audio();
    audio.StopAmbient();
    audio.ResumeBGM();

    // 停止玩法与 UI 快捷键的输入推送
    UISystem& ui = svc_.UI();
    ui.RemoveGameInputSink(this);

    // 局内 UI 交互全部关闭：SetHudInteractive 处理能量图标，
    // DetachAll 反向注销各层输入订阅并弹掉模态
    SetHudInteractive(false);
    overlayLayers.DetachAll();

    // 离开主场景（暂停面板"返回主菜单"、右上角 Home、程序退出等）时，
    // 自动把本局分数写入当前模式的排行榜；已保存过（死亡结算）则跳过
    if (!flow.ScoreSaved()) {
        svc_.Scores().UpdateScore(flow.Score(), flow.Mode());
        flow.MarkScoreSaved();
        std::cout << "Exit MainScene: score " << flow.Score()
                  << " saved to rank (" << ScoreRank::ModeName(flow.Mode()) << ")." << std::endl;
    }
    svc_.Profile().Save(); // 中途退出也保存金币与升级（局末结算处已另存）
    svc_.Ach().Save(); // 同步落盘累计统计（成就解锁时已即存，此处兜底防中途退出丢失）
}

void MainScene::Update(float dt)
{
    // 击杀慢动作（C12 联动）：先以真实 dt 递减计时，再整帧缩放——动画/物理/粒子同步放缓，
    // 输入为边沿检测不受影响；缩放随剩余时间线性缓升，避免硬切回常速。
    player.TickSlowMo(dt);
    dt *= player.TimeScale();

    // 成就解锁提示（任何阶段都可能弹出）
    PollAchievementToasts();

    // ===== 屏幕覆盖层统一驱动 =====
    // UI 交互闸门：入场倒数/结算期间局内按钮与能量图标一律不响应；
    // 结算面板自己的组由 OverLayer 的 ActivateLayer/Reset 管理。
    const bool uiInteractive = !flow.IsGameOver() && !flow.InIntro();
    SetHudInteractive(uiInteractive);
    mainSceneUILayer->SetUpdateEnabled(uiInteractive);
    overlayLayers.Tick(dt); // Toast -> UI -> 结算（插入顺序）

    // 天气事件消息盒：闪烁计时 + 雷击事件轮询（阵风/风暴档位在各自更新点驱动）
    weather.TickFeed(dt);

    if (flow.IsGameOver()) return;

    // ===== 入场流程：转场遮罩 / 倒数期间，世界与操控冻结 =====
    // 障碍不进入屏幕、蝙蝠原地待机振翅（与准备界面同一姿态），天气照常以保持氛围
    if (flow.InIntro()) {
        UpdateIntro(dt);
        weather.SetRainScrollSpeed(0.0f); // 世界静止，水花与冻结地面同步
        weather.AdvanceRain(dt);
        bat->Update(dt);
        return;
    }

    // collisionEnabled 语义: 碰撞是否发生
    if (mainSceneUILayer->GetPauseState() || !flow.CollisionEnabled()) {
        weather.SetRainScrollSpeed(0.0f);
        if (!flow.CollisionEnabled()) {
            weather.AdvanceRain(dt);
            bat->Update(dt);
        }
        return;
    }

    // 本帧只读快照：各子系统共享同一份状态，不反向读场景字段
    const StageFrame frame = BuildFrame(dt);

    // 三段视差条带统一推进：地面滚动速度 == 障碍物速度（随难度递进）
    parallax.Scroll(dt,
                    svc_.Cfg().GetBackgroundSpeed(),
                    svc_.Cfg().GetForegroundSpeed(),
                    GetCurrentObstacleSpeed());
    obstacles.Update(dt, frame);    obstacles.UpdateFlying(dt, frame); // 被撞飞障碍的抛物线飞行与回收
    items.Update(dt, frame);           // 道具漂移 + 巨化星
    player.UpdateGiant(dt);            // 巨化计时/缩放收尾
    // ===== 巨化撞飞：与管道列横向重叠且纵向相交（只撞飞实际碰到的那根） =====
    {
        const PlayerAbilities::GiantBody giant = player.Giant();
        if (giant.active) obstacles.SmashOverlapping(giant.center, giant.halfSize);
    }
    // 输入意图（由 UISystem 推送的本帧快照）：俯冲（S/↓）由玩家子系统读取
    weather.UpdateGust(dt, frame, params()); // 阵风（A4-b）：只算推力，由玩家子系统在积分前施加
    weather.UpdateFx(dt); // 风线可视化（仅生效期显示；预警期无气流）
    player.Update(dt, frame, weather.GustForce()); // 蝙蝠运动 + 俯冲 + 护盾/能量计时
    UpdateTimeRush(dt);   // 限时冲刺倒计时（B9）
    // 地面滚动/水花漂移速度与障碍物同步（难度递进后不再恒为 200）
    weather.SetRainScrollSpeed(GetCurrentObstacleSpeed());
    weather.AdvanceRain(dt);

    // ===== 能量护盾计时在 PlayerAbilities::Update 内推进 =====

    // ===== 连击窗口倒计时（A3） =====
    flow.TickCombo(dt);
    fx.Update(dt); // 飘字上浮/淡出 + 抖动衰减

    // ===== 难度跨档提示（A1）+ 天气联动（A4-c） =====
    {
        int tier = 0;
        if (flow.UpdateTier(tier)) {
            weather.ApplyForTier(tier, params()); // 雨强/雷频/天色随档位变化
            const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();
            const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
            SpawnFloatText("风暴渐强 · 提速！",
                           Vec2(WIN_WIDTH * 0.5f, WIN_HEIGHT - 70.0f),
                           UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 28.0f, 2.0f);
            // 消息盒：风暴档位为持续状态（本局内不回落），短暂闪烁后常驻显示（天气事件消息盒）
            weather.Post(WMsgStorm, "风暴 " + std::to_string(tier) + " 级", 0.8f, true);
        }
    }

    // ===== 天色遮罩渐变 + 段落期雨强下限（怪物 scrimBoost / minRainIntensity） =====
    weather.UpdateAmbient(dt, frame);

    // ===== 怪物（EnemyDirector）：上下文下行 / 输入意图 / 每帧驱动 =====
    // 暂停/入场/死亡动画期间随上方门控不推进（Update 早退即冻结）
    {
        EnemyContext ectx;
        ectx.batPos = bat->GetPosition();
        // 等效碰撞半径=蝙蝠当前横向尺寸一半（巨化期 bat scale 已放大，无需再乘系数）
        ectx.batRadius = bat->GetScale().x * 0.5f;
        ectx.batInvincible = player.IsInvincible();
        ectx.batGiant = player.IsGiant();
        ectx.score = flow.Score();
        ectx.timeLeft = flow.Timed() ? flow.TimeLeft() : -1.f; // <0 = 非限时
        ectx.mode = flow.Mode();
        ectx.terrainY = terrain_height;
        ectx.screenW = (float)svc_.Cfg().GetScreenWidth();
        ectx.screenH = (float)svc_.Cfg().GetScreenHeight();
        // 输入意图（取自 UISystem 推送的本帧已裁决快照：悬停在控件上的右键不会下发）
        ectx.firePrimary = gameInput.PointerPressed(GLFW_MOUSE_BUTTON_RIGHT) ||
                           gameInput.Pressed(GLFW_KEY_F);
        ectx.diveRequested = gameInput.Pressed(GLFW_KEY_S) ||
                             gameInput.Pressed(GLFW_KEY_DOWN);
        ectx.batDiving = player.Diving();      // 俯冲急坠中（怪物本体接触判定：俯冲撞击）
        ectx.batParry = player.DiveParryWindow(); // 俯冲无敌帧窗口（弹反判定，§4.1 三期）
        ectx.fxFx      = this; // 三个角色接口各管一段职责，怪物只取自己需要的
        ectx.fxEconomy = this;
        ectx.fxPlayer  = this;
        enemies.Update(dt, ectx);
    }

    items.CheckPickup(bat->GetPosition(), bat->GetScale());

    if (flow.CollisionEnabled()) {
        std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
        if (!batSprite || batSprite->GetInvincibleState()) return;
        bool hitByObstacle = obstacles.CheckCollision(bat->GetPosition(), bat->GetScale());
        bool hitByLightning = weather.CheckStrikeHit(bat->GetPosition()); // A4-a：落雷危险窗口内的雷击判定
        if (hitByObstacle || hitByLightning) {
            if (hitByLightning) {
                SpawnFloatText("遭雷击！", bat->GetPosition() + Vec2(0.0f, 46.0f),
                               0.87f, 0.35f, 0.35f, 24.0f);
                ReportAchievement("lightning_death", 1);
            }
            KillPlayer(hitByLightning ? "lightning" : "collision"); // 统一致死入口（含怪物复位/音效/死亡动画/结算）
        }
    }
}

void MainScene::Render(SpriteRenderer& renderer)
{
    // ===== 全局抖动（B6-b 反馈）：平移投影使所有世界渲染跟随震动（ImGui UI 不受影响） =====
    // 抖动投影同源分发给 SpriteRenderer（普通精灵）与 RenderContext（雨/风/闪电），
    // 世界内所有元素一致震动；无抖动时渲染器取标准正交投影
    const float SW = (float)svc_.Cfg().GetScreenWidth();
    const float SH = (float)svc_.Cfg().GetScreenHeight();
    float offX = 0.0f, offY = 0.0f;
    const bool shaking = fx.ShakeOffset(offX, offY);

    Mat4 projection{};
    Ortho(0.0f, SW, 0.0f, SH, projection.m);
    if (shaking) {
        Ortho(offX, SW + offX, offY, SH + offY, projection.m);
        renderer.SetProjection(projection.m);
    }
    RenderContext ctx{ renderer, projection };

    // ===== 世界渲染槽位管线（方案二）：WorldSlot 枚举顺序即绘制顺序 =====
    // 场景自有内容与天气渲染器在同一张槽位表里排位；雨的 4 档深度穿插
    // （远景淡而慢、近景实而快，营造纵深）由渲染器的槽位声明表达，
    // 不再依赖手写调用序列。新增渲染器只需注册并声明槽位，本函数零改动。
    for (int i = 0; i < (int)WorldSlot::Count; ++i) {
        const WorldSlot slot = (WorldSlot)i;
        weather.RenderSlot(ctx, slot); // 天气渲染器：只画认领的槽位（含天色遮罩）
        DrawWorldSlot(ctx, slot);      // 场景自有内容：贴图组与玩法子系统
    }

    // 恢复未抖动的投影（其它场景共用同一渲染器）
    if (shaking) {
        Ortho(0.0f, SW, 0.0f, SH, projection.m);
        renderer.SetProjection(projection.m);
    }

    // 覆盖层按阶段插入：Toast 在场景 HUD 之前，UI/结算在之后。
    // UI 层是否绘制按渲染时刻的 gameOver 判定，保持与旧版逐帧检查一致。
    overlayLayers.Draw(renderer, LayerRenderPhase::BeforeHud);
    DrawImGUILayout();
    // DrawImGUILayout 内的调试入口可能同步 ChangeScene，此时栈已 Detach，
    // 不能再把 UI 层重新打开，否则会在新场景上多画一帧旧 UI。
    if (overlayLayers.IsAttached()) {
        mainSceneUILayer->SetRenderEnabled(!flow.IsGameOver());
        overlayLayers.Draw(renderer, LayerRenderPhase::AfterHud);
    }
}

// 场景自有内容的槽位填充：天气渲染器不认领的槽位在此绘制；
// 双方都不认领的槽位自然跳过。槽位到内容的映射即渲染层级的文档。
void MainScene::DrawWorldSlot(const RenderContext& ctx, WorldSlot slot)
{
    switch (slot) {
        case WorldSlot::Background: ctx.sprites.Draw_UV(parallax.Background()); break;
        case WorldSlot::Foreground: ctx.sprites.Draw_UV(parallax.Foreground()); break;
        case WorldSlot::Terrain:    ctx.sprites.Draw_UV(parallax.Terrain());    break;
        case WorldSlot::Obstacles:  if (isObstacleVisible) obstacles.Render(ctx.sprites); break; // 障碍物可见时才渲染
        case WorldSlot::Items:      items.Render(ctx.sprites);            break; // 道具 + 巨化星
        case WorldSlot::Enemies:    enemies.RenderWorld(ctx.sprites);     break; // 怪物世界层（枭王：血月/本体/羽刃/水晶/音爆）
        case WorldSlot::Player:     ctx.sprites.Draw_UV(bat);             break; // 蝙蝠（风线之后：气流贴玩法平面，蝙蝠保持清晰）
        default: break; // 其余槽位由天气渲染器认领（雨带0~3/风线/天色遮罩/闪电）
    }
}

void MainScene::Reset()
{
    // 流程状态（分数/连击/限时/入场/门控）先归零，确保下方初始布局使用第 0 档参数
    flow.Reset(params());
    fx.Reset(); // 飘字与抖动归零
    weather.Reset(); // 消息盒/阵风/遮罩 + 雨系统归零
    enemies.OnRunStart(); // 怪物完全复位（须在下方障碍布局前，避免段落期残留规则抑制道具生成）
    items.Reset();        // 道具池/巨化星归零（须在障碍布局前，走廊生成依赖空池）

    float WIN_WIDTH = svc_.Cfg().GetScreenWidth();
    float WIN_HEIGHT = svc_.Cfg().GetScreenHeight();

    // reset bg position：三段条带按地面高度重新铺排（块数由资源决定）
    parallax.Reset(WIN_WIDTH, WIN_HEIGHT, terrain_height, terrain_render_height);

    // 管道重排 + 列间走廊道具：快照取第 0 档（分数已归零）
    obstacles.Reset(BuildFrame(0.0f));

    // reset bat position
    // 与 ReadyScene 的待机蝙蝠同位（270, 580）：转场遮罩以蝙蝠为圆心聚焦，
    // 两场景蝙蝠位置/大小一致，营造"同一只蝙蝠"的连续错觉
    bat->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f + 100.0f);
    std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
    float batSize = svc_.Cfg().GetBatSize();
    bat->SetScale(batSize, batSize); // 巨化中重开时恢复正常尺寸
    batSprite->ResetState();

    // reset variables（分数/门控已在 flow.Reset 中归零）
    isObstacleVisible = true;
    player.Reset(); // 能量/护盾/俯冲/巨化/慢动作归零
    weather.ApplyForTier(GetCurrentDifficultyTier(), params()); // 天气回到当前档位基线（A4-c）

    mainSceneUILayer->Reset();
    mainSceneOverLayer->Reset();
    svc_.UI().ClearWidgetState(hudGroupId); // 能量图标残留高亮复位
}

// 局内 HUD 与顶部按钮排的交互闸门（结算面板组不受影响，由 OverLayer 自管）
void MainScene::SetHudInteractive(bool interactive) {
    energyScope.SetEnabled(interactive);
    mainSceneUILayer->SetInputEnabled(interactive);
}

void MainScene::LoadSpritesFromResourceManager() {
    parallax.Load(svc_.Res());
    bat = svc_.Res().GetSprite("bat");

    obstacles.Load(svc_.Cfg(), svc_.Res());
    items.Load(svc_.Res(), svc_.Audio());
}

void MainScene::DrawImGUILayout() {
    if (flow.IsGameOver()) return; // 死亡时不绘制调试 HUD

    // 转场揭示期（入场 Mask 相位）只露中央圆形视野：主界面 UI 尚未弹出；
    // 遮罩本体由 SceneManager 于场景绘制后统一叠加，此处只控制 HUD 不出现
    if (flow.MaskPhase()) return;

    // 常驻 HUD 第一段：SCORE / 能量 / 金币 / 俯冲（位于天气消息盒之下）
    StageHud::Snapshot snap = BuildHudSnapshot();
    hud.DrawPrimary(snap, *energyIcon);

    // 天气事件消息盒（阵风/雷击/风暴档位）：贴着 SCORE 栏下缘，向下可变高
    weather.DrawMsgBox();

    // 常驻 HUD 第二段：限时倒计时 / 连击（可压在消息盒之上）
    hud.DrawOverlay(snap);

    // 怪物前景 HUD（枭王：血条/心数/声波槽/B2 预警带/右缘渐晕）
    enemies.RenderUI();

    // 落雷预警光柱（A4-a）
    weather.DrawLightningWarning(player.PulseClock());

    // 拾取/贴脸/跨档飘字（A2/A3）
    fx.Draw((float)svc_.Cfg().GetScreenHeight());

    // 3-2-1 倒数大字（倒数期间主界面 UI 已弹出）
    hud.DrawCountdown(snap);

    // 调试面板（保留，但改为默认收起，减少干扰）
    if (showDebugPanel) {
        ImGui::Begin("Scene Control");
        if (ImGui::Button("Go To Ready Scene")) {
            svc_.Scenes().RequestScene(kSceneReady);
            std::cout << "Switched to ready scene..." << std::endl;
        }
        if (ImGui::Button("Reset Game")) {
            player.TryReleaseShield(3.0f); // 调试重开：经统一入口授予开局保护
            Reset();
        }
        ImGui::End();
    }
}

StageHud::Snapshot MainScene::BuildHudSnapshot() const {
    StageHud::Snapshot s;
    // 画布尺寸随快照下发：HUD 不再自查全局配置
    s.screenW = (float)svc_.Cfg().GetScreenWidth();
    s.screenH = (float)svc_.Cfg().GetScreenHeight();
    s.score = (int)flow.Score();
    s.coins = svc_.Profile().GetCoins();
    s.energy = player.Energy();
    s.shieldTimer = player.ShieldTimer();
    s.shieldDuration = player.ShieldDuration();
    s.pulseClock = player.PulseClock();
    s.diveCooldown = player.DiveCooldown();
    s.diveCooldownTotal = player.DiveCooldownTotal();
    s.diving = player.Diving();
    s.timeLeft = flow.TimeLeft();
    s.timed = params().timed;
    s.comboCount = flow.ComboCount();
    s.comboTimer = flow.ComboTimer();
    s.comboWindow = RunFlow::ComboWindow;
    s.comboHudY = params().comboHudY;
    s.counting = flow.Counting();
    s.countdownValue = flow.CountdownValue();
    s.countdownTimer = flow.CountdownTimer();
    s.countdownStep = RunFlow::CountdownStepTime;
    return s;
}


StageFrame MainScene::BuildFrame(float dt) const {
    StageFrame f;
    f.dt = dt;
    f.screenW = (float)svc_.Cfg().GetScreenWidth();
    f.screenH = (float)svc_.Cfg().GetScreenHeight();
    f.terrainY = terrain_height;
    if (bat) {
        f.batPos = bat->GetPosition();
        f.batScale = bat->GetScale();
    }
    auto bsp = std::dynamic_pointer_cast<BatSprite>(bat);
    f.batInvincible = player.IsInvincible();
    f.batGiant = player.IsGiant();
    f.tier = GetCurrentDifficultyTier();
    f.obstacleSpeed = GetCurrentObstacleSpeed();
    f.gapSize = GetCurrentGapSize();

    // 怪物段落规则摊平：子系统不反向依赖 EnemyDirector
    const EncounterRules& rules = enemies.Rules();
    f.freezeOffscreenPipes = rules.freezeOffscreenPipes;
    f.suppressPipeSpawn = rules.suppressPipeSpawn;
    f.suppressItemSpawn = rules.suppressItemSpawn;
    f.pauseGust = rules.pauseGust;
    f.enemyScrimBoost = rules.scrimBoost;
    f.enemyMinRain = rules.minRainIntensity;

    f.input = &gameInput;
    f.timeLeft = flow.TimeLeft();
    f.timed = params().timed;
    return f;
}

// ===== 难度递进（A1） =====

int MainScene::GetCurrentDifficultyTier() const {
    return flow.Tier();
}

float MainScene::GetCurrentObstacleSpeed() const {
    float tier = (float)GetCurrentDifficultyTier();
    return svc_.Cfg().GetObstacleSpeed() *
           (1.0f + RunFlow::SpeedBonusPerTier * tier) * params().speedFactor;
}

float MainScene::GetCurrentGapSize() const {
    float tier = (float)GetCurrentDifficultyTier();
    return svc_.Cfg().GetGapSize() * (1.0f - RunFlow::GapShrinkPerTier * tier);
}

// ===== 连击（A3） =====

int MainScene::ApplyPickupComboBonus() {
    int bonus = flow.ApplyPickupCombo(RunFlow::ComboWindow, RunFlow::ComboMaxBonus);
    ReportAchievement("combo", flow.ComboCount());
    if (bonus > 0) AddScore((size_t)bonus); // 含模式倍率与成就上报
    return bonus;
}

void MainScene::SpawnComboBonusText(int bonus) {
    if (bonus <= 0) return;
    Vec2 p = bat->GetPosition() + Vec2(0.0f, 34.0f);
    SpawnFloatText("+" + std::to_string(bonus) + " 连击x" + std::to_string(flow.ComboCount()), p,
                   UITheme::Moonlight.r, UITheme::Moonlight.g, UITheme::Moonlight.b, 18.0f);
}

void MainScene::OnItemPicked(int itemType) {
    // 收益结算：分数/能量/金币/成就/连击/飘字均走场景统一入口，
    // 保证模式倍率、连击窗口与成就上报不被道具侧绕过
    const ItemField::Effect& e = ItemField::EffectOf(itemType);
    if (e.score > 0) AddScore((size_t)e.score);
    if (e.energy > 0.0f) player.AddEnergy(e.energy);
    svc_.Profile().AddCoins(e.coins);
    ReportAchievement("item_collected", 1);
    SpawnComboBonusText(ApplyPickupComboBonus());
    SpawnFloatText(e.floatText, bat->GetPosition(), e.r, e.g, e.b, e.fontSize);
}

// ===== 飘字反馈（A2/A3） =====

void MainScene::SpawnFloatText(const std::string& text, Vec2 worldPos,
                               float r, float g, float b, float fontSize, float life) {
    fx.Spawn(text, worldPos, r, g, b, fontSize, life);
}

// ===== 入场流程（转场遮罩 + 3-2-1 倒数） =====

void MainScene::UpdateIntro(float dt) {
    // 遮罩阶段：转场动画由 SceneManager（TickTransition）驱动，本场景只等待其结束；
    // 揭示完毕由 RunFlow 转入 3-2-1
    const RunFlow::IntroTick ev = flow.TickIntro(dt, svc_.Scenes().IsTransitionActive());
    if (ev.becameCounting) {
        svc_.Audio().PlaySFX(SFXToFile(SFX::Countdown), 0.5f);
    }
    if (ev.countdownAdvanced) {
        svc_.Audio().PlaySFX(SFXToFile(SFX::Countdown), 0.5f); // 新数字出现时滴答
    }
    if (ev.finished) {
        // 正式开始：解锁操控并给予开局保护（经统一入口授予）
        player.TryReleaseShield(100.f);
        svc_.Audio().PlaySFX(SFXToFile(SFX::Start), 0.9f);
        SpawnFloatText("开始！", bat->GetPosition() + Vec2(0.0f, 60.0f),
                       UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 26.0f, 1.2f);
        std::cout << "Intro finished. Game starts." << std::endl;
    }
}

// ===== 天气事件消息盒的 IFeedbackFx 实现 =====

void MainScene::PostWeatherMsg(int slot, const std::string& text, float flashTime, bool autoSteady) {
    weather.Post(slot, text, flashTime, autoSteady);
}

void MainScene::RemoveWeatherMsg(int slot) {
    weather.Remove(slot);
}

void MainScene::RequestLightningStrike(float worldX) {
    // 怪物侧脚本落雷：转发到雨系统既有管线（预警/危险窗/音效/渲染），场景不解释怪物意图
    if (auto rs = svc_.Res().GetRainSystem())
        rs->TriggerStrikeAt(worldX);
}

// ===== 多模式（B9）：模式差异的唯一事实源已下沉到 gameplay/StageParams.cpp =====
// 本场景只经 params() / ParamsOfIndex() 读取，不再持有表本体，
// 也不再负担「新增模式时到场景里补一行」的职责。

void MainScene::AddScore(size_t base) {
    flow.AddScore(base, params().scoreFactor);
    const long long score = (long long)flow.Score();
    // 通用与模式专属成就均由 IGameMode 策略声明；新增模式无需改动此处
    for (const ScoreAchievement& a :
         ModeRegistry::Instance().Get(flow.Mode()).ScoreAchievements(score)) {
        ReportAchievement(a.eventKey, a.value);
    }
}

void MainScene::UpdateTimeRush(float dt) {
    if (flow.TickTimer(dt)) EndRunByTimeout(); // 时间耗尽直接结算
}

// 局末结算（死亡动画结束 / 限时超时共用）：规则与落盘交给 RunSettlement，
// 本场景只负责把结果演出来（破纪录音效 / 金币飘字 / 结算面板）
void MainScene::SettleRunResults() {
    const SettlementResult r = RunSettlement::Apply(flow, svc_.Scores(),
                                                    svc_.Profile());
    if (r.newRecord) {
        svc_.Audio().PlaySFX(SFXToFile(SFX::Record), 1.0f);
    }
    if (r.coinBonus > 0) {
        const float W = (float)svc_.Cfg().GetScreenWidth();
        const float H = (float)svc_.Cfg().GetScreenHeight();
        SpawnFloatText("金币 +" + std::to_string(r.coinBonus),
                       Vec2(W * 0.5f, H - 240.0f),
                       0.95f, 0.82f, 0.35f, 24.0f, 2.0f);
    }

    mainSceneOverLayer->SetCurrentScore((int)r.score);
    mainSceneOverLayer->SetBestScore(r.best);
    mainSceneOverLayer->ActivateLayer();
}

void MainScene::EndRunByTimeout() {
    if (flow.IsGameOver()) return;
    std::cout << "Time's up! Final score: " << flow.Score() << std::endl;
    svc_.Audio().PlaySFX(SFXToFile(SFX::GameOver), 1.0f);
    const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    SpawnFloatText("时间到！", Vec2(WIN_WIDTH * 0.5f, WIN_HEIGHT - 200.0f),
                   UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 30.0f, 2.0f);

    SettleRunResults(); // 破纪录音效/写榜/金币结算/结算面板（与死亡结算共用，无死亡动画）

    flow.SetCollisionEnabled(false);
    flow.SetGameOver(true);
}

// ===== 表现服务（IFeedbackFx / IRunEconomy / IPlayerCommands）：全部为既有能力的薄转发 =====
// 契约见 docs/怪物系统架构设计.md §4.3；怪物经 ctx 的三个窄指针上行，场景不解释怪物事件

void MainScene::FloatTextAt(const std::string& text, Vec2 pos,
                            float r, float g, float b, float size, float life) {
    SpawnFloatText(text, pos, r, g, b, size, life); // 纯透传（颜色词汇与 SpawnFloatText 一致）
}

void MainScene::PlaySFX(int sfxId, float volume) {
    svc_.Audio().PlaySFX(SFXToFile(static_cast<SFX>(sfxId)), volume);
}

void MainScene::Shake(float intensity) {
    TriggerShake(intensity);
}

// PostWeatherMsg/RemoveWeatherMsg 的接口实现即场景既有同名方法，无需另写转发

void MainScene::AddCoins(int n) {
    svc_.Profile().AddCoins((long long)n);
    svc_.Profile().Save(); // 与原击杀奖励路径一致：入账即落盘
}

void MainScene::AddTimeLimit(float sec) {
    flow.AddTimeLimit(sec, kTimeRushDuration); // 非限时模式在 RunFlow 内为空操作
}

void MainScene::ReportAchievement(std::string_view eventKey, long long value) {
    svc_.Ach().Report(eventKey, value);
}

// 统一致死入口（架构设计阶段 0）：碰撞/落雷致死与怪物致死共用同一流程，
// 原怪物致死入口与碰撞分支的平行实现由此合一
void MainScene::KillPlayer(const char* reason) {
    if (!flow.CollisionEnabled()) return; // 已在死亡流程中
    flow.SetCollisionEnabled(false);      // 禁止后续碰撞/受击再触发
    player.SetLocked(true);        // 死亡后禁止护盾等主动技能
    enemies.OnBatDead();           // 怪物全员复位（段落中止，再降临计时从头开始）
    std::cout << "Killed by " << reason << ". Switching to death animation." << std::endl;
    svc_.Audio().PlaySFX(SFXToFile(SFX::Death), 1.0f);
    std::shared_ptr<BatSprite> batSprite = std::dynamic_pointer_cast<BatSprite>(bat);
    if (!batSprite) return;
    batSprite->OnBatHit(); // 死亡动画
    bat->setAnimationCallback([this]() {
        flow.SetGameOver(true); // 动画结束后设置游戏结束标志
        weather.SetRainScrollSpeed(0.0f); // 地面已静止，水花漂移与地形偏移同步冻结
        SettleRunResults(); // 破纪录音效/写榜/金币结算/结算面板（与超时结算共用）
        svc_.Audio().PlaySFX(SFXToFile(SFX::GameOver), 1.0f);
        std::cout << "Death animation finished. Game Over." << std::endl;
    });
}

void MainScene::BounceBat(float dx, float dy) {
    player.Bounce(dx, dy);
}

// 终止巨化（巨化冲撞命中怪物后由怪物侧经 fx 申请；自然到期亦走此处统一还原）
void MainScene::EndGiant() {
    player.EndGiant();
}

void MainScene::OnEnemyItemCollected(Vec2 /*pos*/) {
    // 预留：怪物道具接通连击/成就通道（枭王设计文档 §4.5）；一期行为不变
}

// 蝙蝠受击无敌授予（怪物侧受击模型 §6）：闪烁反馈由 BatSprite 无敌期辉光脉动承担。
// Timer::Start 会无条件重置计时，故仅在能延长时授予——防止把更长的护盾/巨化/俯冲无敌缩短（§6 护盾照常消耗）。
void MainScene::GrantBatInvuln(float dur) {
    player.GrantInvuln(dur);
}

// 击杀慢动作：取较长请求即可；缩放曲线见 PlayerAbilities::TimeScale
void MainScene::TriggerSlowMo(float dur) {
    player.TriggerSlowMo(dur);
}

// ===== 全局抖动（B6-b 反馈） =====

void MainScene::TriggerShake(float intensity) {
    fx.Shake(intensity);
}

// ===== 成就解锁提示（B7） =====

void MainScene::PollAchievementToasts() {
    AchievementToast toastInfo{};
    if (!svc_.Ach().PopPendingToast(toastInfo)) return;
    const char* name = "?";
    for (const auto& def : Achievements::Defs()) {
        if (def.id == toastInfo.id) {
            name = def.name;
            break;
        }
    }
    achievementToast->SetText("成就达成 · " + std::string(name) +
                              " Lv." + std::to_string(toastInfo.level));
    achievementToast->SetColor(Vec3{0.941f, 0.722f, 0.314f});
    achievementToast->Activate();
}
