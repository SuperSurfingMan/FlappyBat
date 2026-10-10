#pragma once
#include "Scene.h"
#include <algorithm>
#include <vector>
#include <string>
#include <array>

#include "RainSystem.h"
#include "../Point2D.h"
#include "../Config.h"
#include "../RainRenderer.h"
#include "../LightningRenderer.h"
#include "../WindRenderer.h"
#include "../GameMode.h"
#include "../enemy/EnemyDirector.h"     // 怪物框架（含 IEnemy/EncounterRules 契约）
#include "../enemy/IStageFeedback.h"    // MainScene 实现表现服务接口（需完整类型）

class RainSystem;
class Layer;

class MainScene : public Scene, public IStageFeedback {
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
    void SetMode(GameMode m) { mode = m; }

    // 模式差异的唯一事实来源：所有随模式变化的数值集中在这张参数表，
    // 调用端一律经 params() 取用，不再出现散落的 mode == 分支
    struct ModeParams {
        const char* name;          // 模式名（日志用）
        float speedFactor;         // 障碍物速度倍率（乘在难度递进曲线上）
        int   scoreFactor;         // 得分倍率（风暴之夜双倍）
        const char* scoreEvent;    // 模式专属成就上报事件（nullptr = 无）
        float gustIntervalMin;     // 阵风间隔下限
        float gustIntervalMax;     // 阵风间隔上限
        float gustForceFactor;     // 阵风力度倍率
        bool  stormWeather;        // true = 风暴天气常驻（跳过难度档位曲线）
        float stormRain;           // 常驻雨强（stormWeather 时）
        float stormLightningMin;   // 常驻落雷间隔下限（stormWeather 时）
        float stormLightningMax;   // 常驻落雷间隔上限（stormWeather 时）
        float stormScrim;          // 常驻天色遮罩透明度（stormWeather 时）
        bool  timed;               // 是否限时
        float timeLimit;           // 限时秒数（timed=false 时仅作为 Reset 初值）
        float comboHudY;           // 连击 HUD 的 y（限时模式顶部让位给倒计时）
        float lightningWarnLead;   // 落雷预警提前量：雷劈下前的虚线预警时长（经典最长，随难度递减）
    };
    static const ModeParams& ParamsOf(GameMode m);
    const ModeParams& params() const { return ParamsOf(mode); }

private:
    bool CollisionDetect();
    void CheckCollectableItemCollision();
    void LoadSpritesFromResourceManager();

    std::vector<std::shared_ptr<Sprite>> backgroundSprites;
    std::vector<std::shared_ptr<Sprite>> foregroundSprites;
    std::vector<std::shared_ptr<Sprite>> terrainSprites;
    std::vector<std::shared_ptr<Sprite>> obstaclesUpPool;
    std::vector<std::shared_ptr<Sprite>> obstaclesDownPool;
    
    std::vector<std::shared_ptr<Sprite>> mosquitoes;
    std::vector<std::shared_ptr<Sprite>> fireflies;
    std::vector<std::shared_ptr<Sprite>> kiwis;
    std::array<bool, 3> mosquitoesUsed;
    std::array<bool, 3> fireflyUsed;
    std::array<bool, 3> kiwiUsed;

    // ===== 道具效果表（A2 激活 + B8 货币）：蚊子/萤火虫/猕猴桃的收益与反馈统一收敛 =====
    enum ItemType { ItemMosquito = 0, ItemFirefly = 1, ItemKiwi = 2, ItemTypeCount = 3 };
    struct ItemEffect {
        int   score;            // 直接加分（蚊子 +1，其余 0）
        float energy;           // 能量增量（蚊子 0）
        int   coins;            // 拾取金币（B8）
        const char* floatText;  // 拾取飘字
        float r, g, b;          // 飘字颜色
        float fontSize;
    };
    static const ItemEffect& EffectOf(int itemType);
    // 道具对象池/占用标志的统一访问（更新/渲染/生成/重置共用一份循环，替代三套重复代码）
    std::vector<std::shared_ptr<Sprite>>* ItemPool(int itemType);
    std::array<bool, 3>* ItemUsed(int itemType);

    std::shared_ptr<Sprite> bat;

    std::array<Point2D, 3> obstacleUpCollisionUVcords; // 用于碰撞检测的障碍物关键点
    std::array<Point2D, 3> obstacleDownCollisionUVcords;
    std::array<Point2D, 2> batCollisionUVcords;

    std::shared_ptr<Layer> mainSceneUILayer;
    std::shared_ptr<Layer> mainSceneOverLayer;

    void UpdateBackground(float dt);
    void UpdateForeground(float dt);
    void UpdateTerrain(float dt);
    void UpdateObstacles(float dt);
    void UpdateBat(float dt, bool diveRequested);
    void UpdateItems(float dt);

    // 当前鼠标是否位于可交互 UI（按钮排/暂停面板按钮/充满时的能量图标）上：
    // 这类点击由按钮消费，不应同时触发蝙蝠振翅
    bool IsPointerOverInteractiveUI() const;
    // 能量图标（闪电）的命中矩形，绘制与点击判定共用一份几何
    void GetEnergyIconRect(float& cx, float& cy, float& size) const;

    void RenderItems(SpriteRenderer& renderer);
    void DrawImGUILayout();
    // ===== 常驻 HUD 布局 =====
    // SCORE：左上角紧邻帧率 overlay 下方；金币：帧率 overlay 右侧贴 FPS 框；
    // 能量组 + 俯冲长条：窗口底部中央并排一行；天气盒：贴着 SCORE 栏下缘。
    // 布局锚点统一由 MainScene.cpp 匿名命名空间的常量与 FpsBox*/ScorePanelCy()/WeatherBoxTopY()/BottomRowCy() 给出
    void DrawScoreHUD();  // 左上角，紧邻帧率显示下方
    void DrawEnergyHUD(); // 窗口底部中央并排行左段（图标+槽）

    void AddEnergy(float amount);
    // 授予无敌的统一入口（MainScene 内不再直接调用 batSprite->SetInvincible）：
    // freeDuration <= 0 玩家主动释放护盾（能量闸门/护盾计时/HUD/音效）；
    // freeDuration >  0 场景授予的免费无敌（开局与重开倒数保护、调试重开、巨化），不耗能量、不占护盾 HUD
    void TryReleaseShield(float freeDuration = 0.0f);
    void DrawDiveHUD(); // 俯冲长条（窗口底部中央，「俯冲」文字 + 沿长条环绕的冷却描边，B6-a）

    // ===== 护盾升级参数（B8：PlayerProfile 升级等级影响） =====
    static constexpr float ShieldBaseDuration = 3.0f;    // 基础持续时间
    static constexpr float ShieldDurationPerLevel = 0.4f; // 每级 +0.4s（满级 5 级 -> 5.0s）
    static constexpr float ChargeMultPerLevel = 0.1f;    // 每级充能速度 +10%（满级 ×1.5）
    float GetShieldDuration() const;    // 含升级等级
    float GetChargeMultiplier() const;  // 含升级等级
    void DrawCoinsHUD();                // 金币 HUD（帧率 overlay 右侧，贴 FPS 框）

    // ===== 难度递进（A1）：速度/间隙随分数分档变化 =====
    int GetCurrentDifficultyTier() const;
    float GetCurrentObstacleSpeed() const;
    float GetCurrentGapSize() const;

    // ===== 雷击威胁（A4-a 三修：折线精确判定） =====
    // 预警为沿雷折线的半透明虚线；判定为蝙蝠圆 vs 主分支各段的点到线段距离，
    // 走廊宽度逐段等于雷线视觉粗细（护盾/无敌免疫；A/D/←/→ 或纵向均可躲避）
    bool CheckLightningStrikeHit();
    void DrawLightningWarning(); // 折线虚线预警（ImGui 前景层，落点「雷」字钳制在窗口内）

    // ===== 阵风（A4-b） =====
    void UpdateGust(float dt);

    // ===== 天气联动（A4-c） =====
    void ApplyWeatherForTier(int tier); // 雨强/雷频/天色遮罩随难度档位变化

    // ===== 连击（A3）：窗口内连续拾取道具获得额外加分 =====
    int ApplyPickupComboBonus();      // 连击计数，返回本次额外加分
    void SpawnComboBonusText(int bonus);

    // ===== 飘字反馈（A2/A3）：拾取/贴脸/跨档提示 =====
    void SpawnFloatText(const std::string& text, Vec2 worldPos,
                        float r, float g, float b,
                        float fontSize = 20.0f, float life = 1.0f);
    void UpdateFloatingTexts(float dt);
    void DrawFloatingTexts();
    void DrawComboHUD();

    bool GenerateTargetItemAt(int itemType, Vec2 position);
    void GenItem(Vec2 position);
    // 在两列障碍物之间的走廊内随机生成道具（按概率降频；高度限制在安全空域）；
    // 同时以极低概率+冷却生成巨化星（B6-b）
    void TrySpawnCorridorItem(float leftColumnX, float rightColumnX);

    // ===== 多模式（B9）：差异全部收敛到 ParamsOf 参数表（见类开头） =====
    GameMode mode = GameMode::Classic;
    static constexpr float TimeRushDuration = 60.0f; // 限时冲刺时长（各模式 Reset 的倒计时初值）
    float timeLeft = TimeRushDuration;               // 剩余时间（仅限时模式消耗）
    void UpdateTimeRush(float dt);
    void EndRunByTimeout();            // 时间耗尽直接结算（无死亡动画）
    void DrawModeTimerHUD();           // 限时冲刺倒计时 HUD
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
    void AddCoins(int n) override;
    void AddTimeLimit(float sec) override;
    void ReportAchievement(AchievementEvent event, long long value) override;
    void KillPlayer(const char* reason) override; // 统一致死入口：碰撞/落雷/怪物共用
    void BounceBat(float dx, float dy) override;
    void OnEnemyItemCollected(Vec2 pos) override;
    void EndGiant() override; // 巨化冲撞命中怪物后立即结束巨化（还原体型/终止无敌）
    void GrantBatInvuln(float dur) override; // 蝙蝠受击无敌授予（闪烁反馈；不缩短既有更长无敌）
    void TriggerSlowMo(float dur) override;  // 击杀慢动作（整帧时间缩放，线性缓升恢复）

    // ===== 巨型蝙蝠（B6-b） =====
    static constexpr float GiantDuration = 5.0f;   // 巨化持续
    static constexpr float GiantScaleFactor = 2.0f;
    float giantTimer = 0.0f;
    int runSmashCount = 0;      // 本局撞飞管道数（成就，按根计数）
    int runNearMissCount = 0;   // 本局贴脸次数（成就）
    std::shared_ptr<Sprite> giantOrb; // 巨化星道具精灵
    bool orbUsed = false;
    float orbCooldown = 20.0f;  // 距下一次巨化星可生成的冷却
    float orbBobTimer = 0.0f;   // 星体炫彩变色/发光脉冲时钟
    bool IsGiantActive() const { return giantTimer > 0.0f; }
    void ActivateGiantMode();
    void UpdateGiant(float dt);     // 计时、缩放收尾、撞飞判定
    void UpdateOrb(float dt);       // 星体移动/炫彩变色/拾取判定
    void SmashObstacles(size_t i, bool smashUp, bool smashDown); // 只撞飞实际碰到的那根（可同时两根）
    void UpdateFlyingObstacles(float dt); // 单管飞行推进与回收

    // 被撞飞障碍物的飞行状态（与对象池一一对应；飞行中脱离常规滚动/碰撞/计分）
    // 每根管道独立飞行：巨化蝙蝠只撞飞碰到的那根（上/下），另一根继续常规滚动
    struct FlyingPipe {
        bool flying = false;
        Vec2 vel;              // 初速度
        float spin = 0.0f;     // 旋转角速度
        float angle = 0.0f;    // 当前旋转角
        float life = 0.0f;     // 飞行时长（淡出/超时判定）
    };
    struct FlyingState {
        FlyingPipe up;    // 上管飞行状态
        FlyingPipe down;  // 下管飞行状态
    };
    std::array<FlyingState, 5> flyingStates;
    float lastSpawnedGapCenter = 0.0f; // 最近生成列的 gap 中心（飞行回收重插入用）

    // ---- 单管飞行与回收辅助 ----
    bool UpdateFlyingPipe(FlyingPipe& fp, const std::shared_ptr<Sprite>& pipe, float dt); // 单管飞行推进，返回是否结束
    void RejoinFlyingPipe(size_t i, bool isUp);   // 飞行结束且搭档在列：贴回搭档当前列
    void ReinsertFlyingPipe(size_t i, bool isUp, float newX); // 飞行结束且搭档仍在飞：本管重插为新列
    float RightmostLivePipeX(size_t excludePair) const; // 最右未飞行管道 x（excludePair 整对排除）

    // ---- 列状态与几何（整列/单管两用） ----
    void RollColumnState(size_t i);    // 抽取新变体 + 平滑随机 gap（只改 obstacleStates/lastSpawnedGapCenter，不动几何）
    void ReinsertColumn(size_t i, float newX); // 整列重插入（RollColumnState + 双管几何 + 着色）
    void ApplyObstacleGeometryPipe(size_t i, bool isUp, float x, float gapCenter, float gapSize); // 单管几何
    void SetObstacleTintPipe(size_t i, bool isUp, int type); // 单管着色（窄间隙红 tint）

    // ===== 全局抖动（撞飞反馈，C12 子集） =====
    // 通过平移 SpriteRenderer 投影实现：所有世界 Sprite 跟随震动，ImGui UI 不受影响
    float shakeTimer = 0.0f;
    float shakeIntensity = 0.0f;
    float shakeClock = 0.0f;
    static constexpr float ShakeDuration = 0.35f;
    void TriggerShake(float intensity);
    void UpdateShake(float dt);

    // ===== 成就（B7） =====
    std::shared_ptr<Layer> achievementToast; // 解锁提示（ToastLayer）
    void PollAchievementToasts();

    bool collisionEnabled = true;
    bool isObstacleVisible = true;

    bool gameOver = false;

    // ===== 入场流程（转场遮罩 + 3-2-1 倒计时） =====
    // Mask：等待准备界面的聚焦遮罩揭示完毕（仅"开始游戏"路径）；
    // Counting：3-2-1 倒数，结束才解锁操控（重开路径无遮罩、直接进入倒数）；
    // None：正式游玩。入场期间世界冻结、蝙蝠原地待机、天气照常。
    enum class IntroPhase { None, Mask, Counting };
    IntroPhase introPhase = IntroPhase::None;
    float introTimer = 0.0f;    // 当前倒数数字已停留的时间
    int countdownValue = 3;     // 当前倒数数字
    static constexpr float CountdownStepTime = 0.8f; // 每个数字停留时间
    void UpdateIntro(float dt);
    void DrawIntroCountdown();

    bool scoreSavedThisRun = false; // 本局分数是否已写入排行榜（防止重复保存）

    bool showDebugPanel = false; // 调试面板默认收起，按 F1 切换（可选）

    // ===== 能量护盾系统 =====
    // 吃 kiwi 果实积攒能量，充满后按空格/点按图标释放 3 秒无敌
    float energy = 0.0f;              // 0.0 ~ 1.0
    float shieldTimer = 0.0f;         // 无敌剩余时间（>0 表示护盾生效中）
    float uiPulseTimer = 0.0f;        // HUD 动画计时
    static constexpr float EnergyPerKiwi = 1.0f / 3.0f; // 3 个果实充满
    static constexpr float EnergyPerFirefly = 1.0f / 6.0f; // 萤火虫充 1/6 能量（A2）

    // ===== 俯冲（B6-a，二期）：S/↓ 定速急坠 0.18s（约 180px），全程无敌帧，冷却 2.5s =====
    // BOSS 战内俯冲撞上本体 → 10 伤害 + 向左弹开（怪物侧判定）；无敌帧经 TryReleaseShield 免费语义授予
    static constexpr float DiveDuration = 0.18f;    // 急坠时长（下坠约 180px，防贴地触界）
    static constexpr float DiveSpeed = 1000.0f;     // 俯冲垂速（覆盖重力）
    static constexpr float DiveInvulnDur = 0.4f;    // 俯冲无敌帧
    static constexpr float DiveCooldownTime = 2.5f; // 冷却
    float diveTimer = 0.0f;      // >0 = 俯冲急坠中
    float diveIframe = 0.0f;     // >0 = 俯冲无敌帧窗口内（弹反判定用）
    float diveCooldown = 0.0f;   // 冷却剩余

    // ===== 击杀慢动作（C12 联动）：Boss 击杀时整帧时间缩放，线性缓升恢复 =====
    static constexpr float SlowMoScale = 0.35f; // 时间缩放下限（触发瞬间）
    static constexpr float SlowMoDur = 0.7f;    // 默认时长（怪物经 IStageFeedback 请求）
    float slowMoTimer = 0.0f;    // >0 = 慢动作进行中
    float TimeScale() const;     // 当前时间缩放（1 = 常速）

    // ===== 难度递进（A1） =====
    // 每 10 分为一档：障碍物速度 +8%/档（上限 +80%），间隙 -3%/档（上限 -30%）
    static constexpr int DifficultyTierSize = 10;
    static constexpr int MaxDifficultyTier = 10;
    static constexpr float SpeedBonusPerTier = 0.08f;
    static constexpr float GapShrinkPerTier = 0.03f;
    int lastTier = 0; // 已提示过的难度档位（跨档时飘字提示）

    // ===== 连击（A3） =====
    static constexpr float ComboWindow = 6.0f; // 连击窗口：两次拾取间隔超过该时长则断连
    static constexpr int ComboMaxBonus = 3;    // 连击额外加分的上限
    int comboCount = 0;      // 窗口内连续拾取次数
    float comboTimer = 0.0f; // 连击窗口剩余时间

    // ===== 贴脸奖励（A3） =====
    static constexpr float NearMissThreshold = 12.0f; // 越过管道时与边缘的最小间隙阈值
    static constexpr int NearMissBonus = 2;           // 贴脸飞过的额外分数

    // ===== 道具生成（走廊随机点，替代原 gap 中心生成） =====
    static constexpr float ItemSpawnProbability = 0.55f;   // 每条障碍间走廊的生成概率
    static constexpr float ItemCorridorEdgeMargin = 90.0f; // 距障碍物列中心的最小水平距离（半宽 64 + 余量）
    static constexpr float ItemMinCorridorWidth = 30.0f;   // 走廊可用宽度低于此值不生成
    static constexpr float ItemSpawnMinHeight = 90.0f;     // 距地面最小生成高度
    static constexpr float ItemSpawnTopMargin = 110.0f;    // 距天花板最小生成距离

    // ===== 飘字反馈（A2/A3） =====
    struct FloatText {
        std::string text;
        Vec2 worldPos;    // 世界坐标（y 向上）
        float r, g, b;    // 文字颜色
        float fontSize;
        float life;       // 剩余存活时间
        float totalLife;  // 初始存活时间（用于计算淡出）
    };
    std::vector<FloatText> floatingTexts;

    // ===== 雷击威胁（A4-a 三修：折线精确判定） =====
    // 判定宽度逐段等于雷线视觉粗细（锥形收细，见 RainSystem::GetMainBranchWidthAt）；
    // 蝙蝠等效受击半径为雷线走廊之外的额外余量
    static constexpr float LightningBatRadius = 26.0f;

    // ===== 阵风（A4-b） =====
    // 周期性四向气流（上/下/左/右，每次至多生效一种，等概率抽取）：
    // 纵向 -> 累加蝙蝠垂直速度（振翅重置速度=对抗手段）；
    // 横向 -> 以恒定漂移速度叠加到输入横移上（输入每帧覆写横速无法累加，
    //         ×GustHorizontalFactor 缩放保证 200px/s 按键横移可对抗）。
    // 流程：冷却 -> 2s 预警（消息盒闪烁预报，无推力）-> 气流生效（消息转常驻状态行）
    static constexpr float GustIntervalMin = 7.0f;   // 阵风间隔下限
    static constexpr float GustIntervalMax = 13.0f;  // 阵风间隔上限
    static constexpr float GustDurationMin = 1.2f;   // 单次阵风时长下限
    static constexpr float GustDurationMax = 2.0f;   // 单次阵风时长上限
    static constexpr float GustForceMax = 220.0f;    // 最大推力（约重力的 44%）
    static constexpr float GustHorizontalFactor = 0.6f; // 横向风力 -> 漂移速度的缩放
    float gustCooldown = GustIntervalMin; // 距下一次阵风「预警」的倒计时
    float gustTimer = 0.0f;               // 当前阵风剩余时间（>0 表示阵风中）
    Vec2 gustForce = {0.0f, 0.0f};        // 本次阵风推力向量（仅一轴非零：上(0,+)/下(0,-)/左(-,0)/右(+,0)）
    float gustWarnTimer = 0.0f;           // 预警剩余时间（>0 = 预警中，尚未产生推力）
    float gustPendingDuration = 0.0f;     // 预警时已决定的气流时长（生效时转入 gustTimer）

    // ===== 天气事件消息盒（用户需求）：左上角固定区域的事件消息流 =====
    // 所有天气变化（阵风/雷击/风暴档位）在此显示：事件提前 2s 以闪烁消息预警，
    // 状态开始后消息转常驻状态行（如「上升气流」），状态结束移除该行、下方消息上移；
    // 最新消息始终排在第一行。位置贴着 SCORE 栏下缘、向下可变高
    // （能量行/俯冲长条已移至窗口底部中央，盒下移不再受 99px 之外的固定区约束）。
    static constexpr float WeatherWarnLead = 2.0f; // 预警提前量（秒）
    static constexpr size_t WeatherMsgCap = 4;     // 最多同时显示条数
    // 槽位 0~2 为场景自留；怪物槽位自 kEnemyWeatherMsgSlotBase（IEnemy.h）起按 EnemyId 分段
    enum WeatherMsgId { WMsgGust = 0, WMsgLightning = 1, WMsgStorm = 2 };
    struct WeatherMsg {
        int id = 0;
        std::string text;          // 当前文本（预警文案或状态文案）
        float warnTimer = 0.0f;    // 预警闪烁剩余时间
        bool steady = false;       // true = 常驻状态行（不闪烁）
        bool autoSteady = false;   // 闪烁结束后自动转常驻（无持续状态的即时事件用，如风暴档位）
    };
    std::vector<WeatherMsg> weatherMsgs;
    float weatherMsgClock = 0.0f;  // 闪烁动画时钟
    void UpdateWeatherFeed(float dt); // 消息计时 + 雷击事件轮询（阵风/风暴档位在各自更新点驱动）
    void PostWeatherMsg(int id, const std::string& text,
                        float flashTime = WeatherWarnLead, bool autoSteady = false) override; // 新增/刷新预警（置顶闪烁；IStageFeedback 实现）
    void SetWeatherMsgSteady(int id, const std::string& text); // 预警结束转常驻状态行
    void RemoveWeatherMsg(int id) override;                    // 状态结束移除（下方消息上移；IStageFeedback 实现）
    void DrawWeatherMsgBox();                                  // 左上角消息盒绘制

    // ===== 天气联动（A4-c） =====
    std::shared_ptr<Sprite> stormScrimSprite; // 天色变暗遮罩（白色纹理着色）
    float stormScrimAlpha = 0.0f;        // 当前透明度（渐变过渡）
    float stormScrimTargetAlpha = 0.0f;  // 目标透明度（由难度档位决定）
    float weatherRainBaseline = 0.70f;   // 档位雨强基线（ApplyWeatherForTier 写入；段落期雨强下限合成用）

    // ===== 障碍物变体（A5） =====
    struct ObstacleVariantState {
        int type = 0;               // 0 普通 / 1 摆动 / 2 窄间隙
        float gapSize = 100.0f;     // 该对障碍物的间隙大小
        float baseGapCenter = 0.0f; // 摆动基准中心
        float oscillAmp = 0.0f;     // 摆幅（不超过间隙一半，避免道具被管道吞掉）
        float oscillPhase = 0.0f;   // 摆动初相位
        bool nearMissChecked = false; // 是否已对该障碍物做过贴脸判定（蝙蝠可横移，防止反复越过重复触发）
    };
    std::array<ObstacleVariantState, 5> obstacleStates; // 与障碍物对象池一一对应
    float obstacleOscillTime = 0.0f; // 摆动全局时钟
    static constexpr int OscillatingUnlockTier = 2;  // 摆动管道解锁档位
    static constexpr int NarrowUnlockTier = 4;       // 窄间隙管道解锁档位
    static constexpr float OscillSpeed = 1.8f;       // 摆动角速度（rad/s）
    static constexpr float NarrowGapFactor = 0.78f;  // 窄间隙相对当前间隙的比例
    static constexpr float MinNarrowGap = 74.0f;     // 窄间隙下限（蝙蝠 64px + 余量）
    // 窄间隙变体的实际间隙：当前间隙 x0.78，但下限不越过当前间隙本身——
    // 高难度档（9 档起）当前间隙已低于 MinNarrowGap，此时窄间隙不再进一步收缩，
    // 否则 std::clamp(x, 74, 73) 上下界倒挂直接断言崩溃（击杀奖励跳档后必现）
    static float NarrowGapOf(float currentGap) {
        return std::max(currentGap * NarrowGapFactor, std::min(currentGap, MinNarrowGap));
    }
    int RollObstacleVariant() const;
    void ApplyObstacleGeometry(size_t i, float x, float gapCenter, float gapSize);
    void SetObstacleTint(size_t i, int type);

    size_t obstacles_score = 0; // 用于计分

    const float terrain_render_height = Config::GetInstance().GetTerrainRenderHeight();   // 地面贴图的实际大小
    const float terrain_height = Config::GetInstance().GetTerrainHeight(); // 地面高度

    std::shared_ptr<RainSystem> rainSystem;

    // ===== 怪物（EnemyDirector 框架，docs/怪物系统架构设计.md）：场景与具体怪物解耦的唯一通道 =====
    // 枭王（D14 血月枭王）为首个注册怪物；场景只认导演与 IEnemy 契约
    EnemyDirector enemies;

    // 渲染器（与 RainSystem 逻辑分离）
    RainRenderer rainRenderer;
    LightningRenderer lightningRenderer;
    WindRenderer windRenderer; // 阵风可视化：长飘带描气流方向（A4-b 补充）
};
