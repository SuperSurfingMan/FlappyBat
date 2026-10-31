#pragma once
#include "Scene.h"
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <array>

#include "RainSystem.h"
#include "../Point2D.h"
#include "../Config.h"
#include "../GameMode.h"
#include "../StageParams.h"        // 模式参数表（与天气子系统共用的事实源）
#include "../ui/UIWidget.h"          // 局内 HUD 热区控件（能量图标）需完整类型
#include "../ui/GameInput.h"        // 玩法输入快照与接收接口（蝙蝠操控由 UISystem 推送）
#include "../ui/InputScope.h"       // 能量图标组的启用/禁用（与模态约束一致）
#include "../layer/LayerStack.h"    // 屏幕覆盖层组合容器
#include "stage/StageHud.h"          // 常驻 HUD 绘制（纯绘制，快照驱动）
#include "stage/FeedbackFx.h"        // 飘字与全局抖动（纯表现）
#include "stage/WeatherDirector.h"   // 阵风/落雷/消息盒/天色（IRenderer 注册表：雨/雷/风）
#include "../WorldSlot.h"            // 世界渲染槽位（Render 的槽位管线）
#include "stage/PlayerAbilities.h"   // 护盾/俯冲/巨化/慢动作/蝙蝠运动
#include "stage/ObstacleField.h"    // 管道池/变体/碰撞/飞行回收
#include "stage/ItemField.h"        // 道具池/生成/拾取/巨化星
#include "stage/RunFlow.h"          // 模式/分数/连击/限时/入场/门控
#include "../enemy/EnemyDirector.h"     // 怪物框架（含 IEnemy/EncounterRules 契约）
#include "../enemy/IStageFeedback.h"    // MainScene 实现表现服务接口（需完整类型）

class RainSystem;
class MainSceneUILayer;
class MainSceneOverLayer;
class ToastLayer;
struct RenderContext;

class MainScene : public Scene, public IStageFeedback, public IGameInputSink {
public:
    MainScene();
    ~MainScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

    // ===== 多模式（B9）：由 ReadyScene 在进入前设置 =====
    // 枚举本体在 src/GameMode.h（怪物框架需按模式取参，不允许反向依赖场景）；
    // 类内别名保持 MainScene::GameMode 的既有引用方式不变
    using GameMode = ::GameMode;
    void SetMode(GameMode m) { flow.SetMode(m); }

    // ===== IGameInputSink（玩法输入不再由本场景逐帧询问设备） =====
    // UISystem 每帧先做 UI 命中裁决与控件派发，再把“剩下的输入”作为本帧快照推过来；
    // 接收时机在 Scene::Update 之前，因此本帧内任何位置读到的都是同一份已裁决结果
    void OnGameInput(const GameInputFrame& frame) override { gameInput = frame; }
    GameInputFrame gameInput;   // 本帧快照（键位需先经 UISystem::WatchKeys 登记）

    // 模式差异的唯一事实来源：所有随模式变化的数值集中在 StageParams.h，
    // 调用端一律经 params() 取用，不再出现散落的 mode == 分支
    using ModeParams = ::ModeParams;
    const ModeParams& params() const { return ::ParamsOf(flow.Mode()); }

private:
    void LoadSpritesFromResourceManager();

    std::vector<std::shared_ptr<Sprite>> backgroundSprites;
    std::vector<std::shared_ptr<Sprite>> foregroundSprites;
    std::vector<std::shared_ptr<Sprite>> terrainSprites;

    std::shared_ptr<Sprite> bat;

    // 层成员用具体类型持有：直接调用具体 API，避免 static_pointer_cast 向下转型；
    // 基类 Layer 视图留给后续统一遍历的容器使用
    std::shared_ptr<MainSceneUILayer> mainSceneUILayer;
    std::shared_ptr<MainSceneOverLayer> mainSceneOverLayer;
    // 屏幕覆盖层：插入顺序即更新顺序（Toast -> UI -> 结算）；
    // 世界渲染（雨/闪电/风/怪物）由 Render 的 WorldSlot 槽位管线排序，不进入此栈
    LayerStack overlayLayers;

    // ===== 局内 HUD 交互控件 =====
    // 能量图标热区：仅充满时可命中（释放护盾）；未满时不启用，
    // 点击自然透传给振翅。命中判定与事件派发均由 UISystem 完成。
    // 热区控件统一用 shared_ptr 登记（所有权由本场景与 UISystem 共享，不再传址）
    std::shared_ptr<UIWidget> energyIcon = std::make_shared<UIWidget>();
    int hudGroupId = -1;
    InputScope energyScope; // 能量图标组（非模态）

    void UpdateBackground(float dt);
    void UpdateForeground(float dt);
    void UpdateTerrain(float dt);

    // 局内 HUD 交互闸门：同步本场景 HUD 组开关并转发给 UI 图层
    // （入场倒数/转场/结算期间不得让局内按钮抢占鼠标）
    void SetHudInteractive(bool interactive);

    void DrawImGUILayout();
    // 槽位管线（方案二）：场景自有内容的槽位填充（贴图组与玩法子系统）；
    // 天气渲染器认领的槽位已在 WeatherDirector::RenderSlot 内处理
    void DrawWorldSlot(const RenderContext& ctx, WorldSlot slot);
    // HUD 快照：把玩法状态拍平成只读视图，绘制类不反向读场景字段
    StageHud::Snapshot BuildHudSnapshot() const;
    // 子系统帧快照：怪物规则在此摊平为布尔/数值，子系统不认识 EnemyDirector
    StageFrame BuildFrame(float dt) const;

    // ===== 难度递进（A1）：速度/间隙随分数分档变化 =====
    int GetCurrentDifficultyTier() const;
    float GetCurrentObstacleSpeed() const;
    float GetCurrentGapSize() const;

    // ===== 连击（A3）：窗口内连续拾取道具获得额外加分 =====
    int ApplyPickupComboBonus();      // 连击计数，返回本次额外加分
    void SpawnComboBonusText(int bonus);
    // 道具拾取收益结算（ItemField 回调）
    void OnItemPicked(int itemType);

    // ===== 飘字反馈（A2/A3）：拾取/贴脸/跨档提示 =====
    void SpawnFloatText(const std::string& text, Vec2 worldPos,
                        float r, float g, float b,
                        float fontSize = 20.0f, float life = 1.0f);

    // ===== 多模式（B9）：模式与流程状态在 RunFlow，参数表在 StageParams.h =====
    void UpdateTimeRush(float dt);
    void EndRunByTimeout();            // 时间耗尽直接结算（无死亡动画）
    void AddScore(size_t base) override; // 统一计分入口（应用模式倍率并上报成就；IStageFeedback 实现）
    void SettleRunResults();           // 局末结算（破纪录音效/写榜/金币结算/结算面板），死亡与超时共用

    // ===== IStageFeedback（怪物表现服务，docs/怪物系统架构设计.md §4.3）=====
    // 薄转发到场景既有能力；怪物经 ctx.fx 上行，场景不解释怪物事件。
    // 命名避让：接口方法不与场景嵌套类型（FloatText/WeatherMsg 结构体）同名——
    // 成员函数名会遮蔽同名嵌套类型名，故飘字接口取 FloatTextAt；
    // PostWeatherMsg/RemoveWeatherMsg/AddScore 由场景既有同签名方法充当实现（原声明处标注 override）
    void FloatTextAt(const std::string& text, Vec2 pos,
                     float r, float g, float b, float size, float life) override;
    void PlaySFX(int sfxId, float volume) override;
    void Shake(float intensity) override;
    // 天气事件消息盒：薄转发到 WeatherDirector（怪物槽位自 kEnemyWeatherMsgSlotBase 起）
    void PostWeatherMsg(int slot, const std::string& text,
                        float flashTime = 2.0f, bool autoSteady = false) override;
    void RemoveWeatherMsg(int slot) override;
    void AddCoins(int n) override;
    void AddTimeLimit(float sec) override;
    void ReportAchievement(AchievementEvent event, long long value) override;
    void KillPlayer(const char* reason) override; // 统一致死入口：碰撞/落雷/怪物共用
    void BounceBat(float dx, float dy) override;
    void OnEnemyItemCollected(Vec2 pos) override;
    void EndGiant() override; // 巨化冲撞命中怪物后立即结束巨化（还原体型/终止无敌）
    void GrantBatInvuln(float dur) override; // 蝙蝠受击无敌授予（闪烁反馈；不缩短既有更长无敌）
    void TriggerSlowMo(float dur) override;  // 击杀慢动作（整帧时间缩放，线性缓升恢复）

    // ===== 全局抖动（撞飞反馈，C12 子集） =====
    // 通过平移投影实现（SpriteRenderer 与 RenderContext 同源）：所有世界渲染跟随震动，ImGui UI 不受影响
    void TriggerShake(float intensity);

    // ===== 成就（B7） =====
    std::shared_ptr<ToastLayer> achievementToast; // 解锁提示
    void PollAchievementToasts();

    bool isObstacleVisible = true;

    // 入场流程（转场遮罩 + 3-2-1 倒计时）状态在 RunFlow
    void UpdateIntro(float dt);

    bool showDebugPanel = false; // 调试面板默认收起，按 F1 切换（可选）

    const float terrain_render_height = Config::GetInstance().GetTerrainRenderHeight();   // 地面贴图的实际大小
    const float terrain_height = Config::GetInstance().GetTerrainHeight(); // 地面高度

    // ===== 怪物（EnemyDirector 框架，docs/怪物系统架构设计.md）：场景与具体怪物解耦的唯一通道 =====
    // 枭王（D14 血月枭王）为首个注册怪物；场景只认导演与 IEnemy 契约
    EnemyDirector enemies;

    // ===== 表现 / 天气 / 玩家 / 玩法 / 流程子系统 =====
    RunFlow flow;              // 模式/分数/连击/限时/入场/门控（纯数据与规则）
    StageHud hud;              // 常驻 HUD（快照驱动，不持玩法状态）
    FeedbackFx fx;             // 飘字 + 全局抖动
    WeatherDirector weather;   // 阵风/落雷/消息盒/天色（含雨/雷/风渲染器）
    PlayerAbilities player;    // 护盾/俯冲/巨化/慢动作/蝙蝠运动
    ObstacleField obstacles;   // 管道池/变体/碰撞/飞行回收
    ItemField items;           // 道具池/生成/拾取/巨化星
};
