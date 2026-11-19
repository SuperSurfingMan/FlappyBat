#pragma once
#include "enemy/IEnemy.h"
#include "enemy/shared/SpritePool.h"
#include <array>
#include <memory>

class Sprite;
class SpriteRenderer;

// =============================================================================
// D14 血月枭王（EnemyDirector 框架首个怪物；原 src/scene/BossSystem 机械迁移，
// 迁移对照见 docs/怪物系统架构设计.md §8）。
// 状态机 Hidden→Warning→Engagement→Defeat/Leave→Hidden；遭遇期经 ActiveRules
// 接管世界规则（停管道/停道具/管道与边缘不致死）；蝙蝠获 3 心专属血量；
// 玩家以「回声水晶充能的音爆冲击波」输出；技能组一期 B1 羽刃风暴 / B2 血月凝视
// （预警→释放→力竭窗口循环）。玩法设计见 docs/BOSS系统设计文档.md；
// 二期补 B3/B4/俯冲，三期补 B5/B6/弹反。
// =============================================================================
class OwlKing : public IEnemy {
public:
    // ---- 按模式调参（原 MainScene::ModeParams 的 boss 五字段迁入；唯一事实来源归怪物）----
    struct Tuning {
        int   scoreGate;   // 分数门槛（达到即降临）
        float timeGate;    // 非限时=开局秒数门槛；限时=剩余时间降到该值时降临
        int   hpBase;      // 基础生命值
        float returnSec;   // 击败后再次降临间隔；<=0 = 本局不再降临
        float cdScale;     // 技能间隔缩放（风暴之夜 0.8）
        // ---- 击杀奖励（§7.2 按模式差异化；AddScore/AddCoins 走场景统一通道，表内为基础值）----
        struct Rewards {
            int scoreBase;     // 击杀基础得分
            int scorePerHeart; // 剩余心每个加分（0 = 该模式无心数加成）
            int coinBase;      // 击杀基础金币
            int coinPerHit;    // 命中每次加币（0 = 该模式无命中加成）
            int coinHitCap;    // 命中加币部分的封顶值
        } rewards;
    };
    // 按 ModeKey 取调参：未登记的模式使用明确命名的默认调参，
    // 因此新增游戏模式不需要改动任何怪物代码。
    static const Tuning& TuningFor(ModeKey key);

    // ---- 全局攻击欲望（§7.1 扩展，全局唯一切柄）：[0,1]，越高技能释放越频繁 ----
    // 只缩放技能间隔（SkillInterval），不改各阶段技能池与预警/释放/力竭时长（§5 预警底线不动）；
    // 缩放曲线 4^(1-2a) 指数对称于中值：0.5=现行节奏(×1.0)、1.0=×0.25、0.0=×4.0，每 +0.1 欲望 ≈ 间隔 ×0.87。
    // 静态存储（枭王全局唯一），跨实例/跨局生效；交战开场 1.5s 入场节拍不受其影响。
    static constexpr float AggressionDefault = 0.5f;
    static void  SetAggression(float a);  // 全局设置（钳位 [0,1]）
    static float Aggression();            // 钳位读取

    EnemyId Id() const override { return EnemyId::OwlKing; }

    // ---- 形象资源批次开关：legacy 归一化序列帧 / Vector 矢量光栅序列帧 ----
    // 两批素材同构（640 方格、eye_anchor=格正中，见 manifest.json），切表参数共用；
    // 全局静态（枭王唯一），设置页切换，偏好持久化到统一存档（O 标签行）
    enum class ArtBatch { Legacy, Vector };
    static void     SetArtBatch(ArtBatch b);   // 切换并持久化（下帧 ApplyAnimation 重挂动画）
    static ArtBatch GetArtBatch();             // 当前批次
    static void     LoadArtBatchPref();        // 从统一存档读偏好（Init 前调用）

    void Init(const class Config& cfg, class ResourceManager& res,
              const class TuningParams& tuning) override;
    void OnRunStart() override;
    void OnBatDead() override;
    void Update(float dt, const EnemyContext& ctx) override;
    void RenderWorld(SpriteRenderer& renderer) override;
    void RenderUI() override;
    EncounterRules ActiveRules() const override;
    void CaptureSense(EnemySense& out) const override; // 威胁几何上报（AutoPlay 等观察者）

private:
    // ===== 常量（玩法设计文档 §7.3/§5） =====
    static constexpr float BossPosX = 470.f;        // 本体中心 x（右缘半身入画）
    static constexpr float BossHitHalfW = 110.f;    // 本体受击半宽（左缘约 x=360）
    static constexpr float BossH = 300.f;           // 受击判定的纵向半径基准（|dy| < BossH/2；渲染尺寸已归一为 kBodyQuad）
    static constexpr float WarningDur = 5.f;        // 预警期
    static constexpr float DefeatDur = 2.f;         // 击杀演出（4 帧 x 0.5s）
    static constexpr float LeaveDur = 2.f;          // 超时离场淡出
    static constexpr float EngageTimeout = 75.f;    // 交战超时（限时模式由局时限制，不适用）
    static constexpr float EngageFadeIn = 0.8f;     // 降临雾化淡入
    static constexpr float FatigueDur = 2.5f;       // 力竭窗口（受伤 x1.5）
    static constexpr float FatigueAlpha = 0.55f;    // 力竭期半透明
    static constexpr float HitInvulnDur = 1.2f;     // 受击无敌
    static constexpr int   BatHeartsMax = 3;
    static constexpr int   SonicChargeMax = 3;
    static constexpr float SonicSpeed = 500.f;
    static constexpr float SonicCooldown = 0.35f;
    static constexpr int   SonicDamage = 5;         // 音爆伤害（§7.1 调整：25 → 5）
    static constexpr float SonicRecoil = 12.f;      // 发射后坐：蝙蝠向左位移回弹（§4.3）
    static constexpr float CrystalInterval = 4.f;   // 水晶生成间隔
    static constexpr int   CrystalMax = 2;          // 场上水晶上限
    static constexpr int   CrystalCoin = 2;         // 水晶拾取顺带金币（§4.2）
    static constexpr float ShadowDashDur = 0.4f;    // B3 掠影穿屏时长（§5 B3）
    static constexpr float ShadowBandHalf = 70.f;   // B3 暗影带半高（带高 140）
    static constexpr float ScreechActiveDur = 3.0f; // B4 Active 总时长（3 波按 0.6s 间隔发出）
    static constexpr float ScreechSpeed = 220.f;    // B4 音波弧扩张速度
    // B4 缺口半高：缺口高 72px = 蝙蝠碰撞圆直径 64（BAT_SIZE=64 的等效碰撞半径×2）+ 8px 竖向余量
    //（设计要求：显著大于碰撞直径 +5~10px）；判定侧另有 +batR/2 中心宽容，安全条全高约 91px
    static constexpr float ScreechGapHalf = 36.f;
    static constexpr float ScreechGapJitter = 12.f; // B4 三道缺口近似对齐：逐道相对基准高度的随机抖动幅值
    static constexpr int   GiantHitDamage = 20;     // 巨化冲撞伤害（任意方向接触，撞击一次巨化即止）
    static constexpr int   ParryDamage = 10;        // 弹反反伤（子弹重算轨迹命中本体）
    static constexpr float ParrySpeed = 620.f;      // 弹反子弹飞向本体的速度（px/s）
    static constexpr float StormBoltFirst = 0.8f;   // B5 首道雷落点（Active 起始后；预警虚线先行展示）
    static constexpr float StormBoltGap = 0.5f;     // B5 三连雷逐次间隔
    static constexpr float StormCallActiveDur = 1.8f; // B5 Active 总时长（末道雷落地）
    static constexpr float StormSpread = 140.f;     // B5 三落点横向间距
    static constexpr float BloodRainActiveDur = 3.0f; // B6 坠羽持续时长
    static constexpr float BloodRerollCd = 0.5f;    // B6 标记列换批间隔
    static constexpr float BloodSpawnCd = 0.3f;     // B6 每列坠羽生成间隔
    static constexpr float BloodDropSpeed = 420.f;  // B6 坠羽下落速度
    static constexpr float BloodColW = 60.f;        // B6 标记列宽
    static constexpr float BloodColMinGap = 90.f;   // B6 列间最小间距（防标记列重叠）
    static constexpr float BarShakeDur = 0.35f;     // 阶段跃迁血条抖动时长（§8）
    static constexpr float BarShakeAmp = 6.f;       // 阶段跃迁血条抖动幅度（px）
    // ---- 表演层（docs/BOSS美术素材生成方案）：受击 flinch 表演 ----
    // 注：独立瞳孔叠加层已移除——体表红瞳质心与眼贴图质心是两个不同分布，无法像素级对准，
    // 且眼睛状态（蓄力炽亮/力竭黯淡/狂暴）已烘焙在各姿态序列帧内；详见素材方案 §十。
    static constexpr float FlinchDur = 0.45f;       // 受击 flinch 时长（Hit 4 帧 x 0.10s 播完保持末帧；过短读作抽搐）
    static constexpr float FatigueOutDur = 0.40f;   // 力竭收势反向展开时长（FatigueOut 4 帧 x 0.10s）
    // ---- §6/§8 受击模型与反馈 ----
    static constexpr float KnockBackX = 100.f;      // §6 技能命中向左击退
    static constexpr float HeartBreakDur = 0.45f;   // §8 心形碎裂动画时长（放大淡出残影）
    static constexpr float KillSlowMoDur = 0.7f;    // §10.5 击杀慢动作时长（场景侧线性缓升恢复）
    // 归一化本体渲染方形边长：素材已统一 640 方格 + 红瞳锚点=格正中 + 脸宽定标（tools/normalize_owl_sheets.py
    // BODY_QUAD 对应），屏上脸宽与 legacy 拍翼渲染等效；全部动画共用此尺寸 → 状态切换体型/眼睛位置恒定
    static constexpr float kBodyQuad = 460.f;
    static constexpr float DashGhostW = 340.f;      // B3 掠影渲染宽（归一化后掠影内容含于方格内）
    static constexpr float SwoopSlide = 240.f;      // 入离场渲染滑移距离（仅视觉偏移；受击判定仍以 BossPosX 为准）
    // 测试开关：三阶段技能必定选取 B6 血月坠羽（绕过不重复规则），仅强制复验 B6 时置 true。
    // 已验收关闭（false）——B5 雷暴召唤回到三阶段技能池，StormCall 抬翼姿态动画随之生效
    static constexpr bool  TestForceBloodRain = false;

    // 组合根注入（Init 一次，生命周期长于本实例）：窗口几何与美术资源查表、
    // 设置页可改的玩法参数（血量倍率；血条上限随之同步缩放）
    const class Config* cfg_ = nullptr;
    class ResourceManager* res_ = nullptr;
    const class TuningParams* tuning_ = nullptr;

    // ===== 状态机 =====
    enum class Phase { Hidden, Warning, Engagement, Defeat, Leave };
    Phase phase = Phase::Hidden;
    float phaseTimer = 0.f;      // 当前相位已进行时间
    float hiddenTimer = 0.f;     // Hidden 相位累计（等待降临触发）
    float returnTimer = 0.f;     // 击败后等待再降临的累计
    bool everSpawned = false;    // 本局是否已降临过（限时模式不再降临判定用）
    int spawnCount = 0;          // 降临次数（再降临 HP 递增 x1.5，上限 400）
    float worldClock = 0.f;      // 浮游/脉冲全局时钟

    // ===== 战斗数值 =====
    int hp = 0, hpMax = 0;
    int batHearts = BatHeartsMax;
    int combatHits = 0;          // 本场命中次数（金币奖励用）
    int sonicCharge = 1;         // 声波充能（开局赠送 1 段；跨遭遇保留）
    float sonicCdTimer = 0.f;
    float hitInvTimer = 0.f;     // 蝙蝠受击无敌（本系统内部）
    float hitFlashTimer = 0.f;   // 枭王受击闪白
    int   lastStage = 1;         // 上次结算伤害时的阶段（检测阶段跃迁：血条抖动 §8）
    float barShakeTimer = 0.f;   // 血条抖动剩余时长（阶段跃迁触发，§8）
    float bossY = 480.f;         // 本体当前 y

    // ===== 技能循环：间隔 → 预警 → 释放 → 力竭 → 间隔 =====
    enum class Skill { None, FeatherStorm, MoonGaze, ShadowDash, ScreechWave, StormCall, BloodRain };
    enum class SkillState { Idle, Telegraph, Active, Fatigue };
    Skill curSkill = Skill::None;
    Skill lastSkill = Skill::None;
    SkillState skillState = SkillState::Idle;
    float skillTimer = 0.f;      // 当前 SkillState 已进行时间
    float intervalTimer = 1.5f;  // 距下一个技能（首个稍短）

    // B2 血月凝视：光带中心 y 与扫掠方向
    float gazeY = 480.f;
    float gazeDir = 1.f;

    // B3 暗影掠袭：预警带锚定的蝙蝠高度（预警开始时捕获）
    float shadowBandY = 480.f;

    // B4 枭啸音波：三道缺口弧（波体独立于技能状态存续至超时；缺口起手即定、三道近似对齐 ±12px）
    struct Screech { bool alive = false; float age = 0.f; float gapY = 480.f; float centerY = 480.f; };
    std::array<Screech, 3> screeches;
    int screechSpawned = 0;

    // B5 雷暴召唤：三落点（Active 开始时按蝙蝠位置捕获）与已落雷数
    std::array<float, 3> stormXs = {0.f, 0.f, 0.f};
    int stormStruck = 0;

    // B6 血月坠羽：顶部标记列（0.5s 换批）与坠羽池
    std::array<float, 4> bloodCols = {135.f, 215.f, 295.f, 375.f};
    float bloodReroll = 0.f;
    float bloodSpawn = 0.f;
    // pendingAge >= 0：已与蝙蝠判定碰撞、正挂起等待 kParryGrace 弹反窗口；
    // <0 = 正常飞行。parried = 已被弹反、正飞向本体。
    struct BloodDrop {
        bool alive = false;
        bool parried = false;
        Vec2 pos = {0.f, 0.f};
        Vec2 vel = {0.f, 0.f};   // 生成时设为 (0, -BloodDropSpeed)，弹反后重算
        float pendingAge = -1.f;
    };
    std::vector<BloodDrop> bloodDrops;
    std::vector<std::shared_ptr<Sprite>> bloodSprites;

    // ===== 对象池 =====
    struct Feather {
        bool alive = false;
        bool parried = false;
        Vec2 pos;
        Vec2 vel;
        float pendingAge = -1.f; // 同 BloodDrop：碰撞挂起计时
    };
    SpritePool<Feather> feathers;                  // x8（B1 弹体）
    struct Wave { bool alive = false; Vec2 pos; float travel = 0.f; int destroyed = 0; };
    SpritePool<Wave> waves;                        // x3
    struct Crystal { bool alive = false; Vec2 pos; float bob = 0.f; };
    SpritePool<Crystal> crystals;                  // x2
    float crystalTimer = 0.f;

    std::shared_ptr<Sprite> owlSprite;    // 枭王本体（动画由状态机切换，见 Anim）
    std::shared_ptr<Sprite> moonSprite;   // 血月（右上角）
    std::shared_ptr<Sprite> dashSprite;   // B3 掠影专用（独立于本体动画，本体隐身期间绘制）

    // ===== 表演层：本体动画状态机（docs/BOSS美术素材生成方案 §七）=====
    // 每帧由 DesiredAnim 依 状态机/技能循环/狂暴/受击 推导期望动画，ApplyAnimation 按需切表（切表即 Reset）
    enum class Anim { Flap, Death, Cast, Fatigue, FatigueOut, Rage, Flinch, Screech, StormCall, Dash, Swoop };
    Anim  curAnim = Anim::Flap;
    float flinchTimer = 0.f;     // 受击 flinch 剩余时长（DamageBoss 触发，期间覆盖其余动画）
    float fatigueOutTimer = 0.f; // 力竭收势反向展开剩余时长（Fatigue 结束时触发，软接力到下一动画首帧）
    int   heartBreakIdx = -1;    // §8 刚被击碎的心形槽位（碎裂残影动画）
    float heartBreakTimer = 0.f; // 碎裂残影剩余时长

    // ===== 帧内上下文与致死暂存 =====
    const EnemyContext* ctx_ = nullptr;   // 仅 Update 期间有效（内部辅助经此取窄接口）
    IFeedbackFx* lastFxFx_ = nullptr;     // 最近一帧的表现服务（生命周期方法清消息行用；场景存活期内恒有效）
    bool pendingKill_ = false;          // 心数归零：Update 收尾统一 fx->KillPlayer（对齐原事件出队时序）

    // ===== 内部流程 =====
    void EnterWarning(const EnemyContext& ctx);
    void EnterEngagement(const EnemyContext& ctx);
    void EnterDefeat();
    void EnterLeave();
    void EnterHidden(bool returning);

    void UpdateEngagement(float dt, const EnemyContext& ctx);
    void UpdateSkill(float dt, const EnemyContext& ctx);
    void StartSkill(Skill s, const EnemyContext& ctx);
    void UpdateProjectiles(float dt, const EnemyContext& ctx);
    void UpdateCrystals(float dt, const EnemyContext& ctx);
    void DamageBat(const EnemyContext& ctx, float edgeY);
    void DamageBoss(int amount, const Vec2& at);
    // 弹反：把子弹速度重算为「指向本体当前位置」，并统一发反馈/消耗短护盾
    Vec2 VelocityToBoss(Vec2 from) const;
    void OnParrySuccess(const EnemyContext& ctx, Vec2 at);
    bool TryFireSonic(const Vec2& from);  // 音爆发射（firePrimary 意图驱动）
    int Stage() const;                    // 1/2/3 按剩余 HP 比例（狂暴分层）
    float SkillInterval() const;          // 随阶段缩短 x cdScale
    float IntervalFactor() const;         // 攻击欲望 → 技能间隔缩放（4^(1-2a)，全局静态）
    static float s_aggression;            // 攻击欲望静态存储（全局唯一切柄的实现细节）
    float DamageMult() const;             // 力竭期 1.5
    void RollBloodCols(const EnemyContext& ctx); // B6：重掷顶部标记列
    // 技能各状态时长表：UpdateSkill 推进与 CaptureSense 威胁快照共用（改表一处同步）
    static float TelegraphDur(Skill s); // 预警时长（§5：B1 0.8 / B2・B3 1.2 / B4・B5・B6 1.0）
    float ActiveDur() const;            // 释放时长（按 curSkill；B1 即发即竭为 0）
    // ---- 表演层 ----
    void ApplyAnimation();               // 每帧调用：期望动画 != 当前则切表
    Anim  DesiredAnim() const;           // 由 状态机/技能循环/狂暴/受击 推导期望动画
    void  ApplyClip(const std::shared_ptr<Sprite>& s, Anim a, int loopOverride = -1); // 应用动画表条目（切表即 Reset）
    // 表演层辅助：批次感知纹理键（Legacy 直接用原键；Vector 加 "_vec" 后缀）
    static std::string ArtKey(const char* legacyKey);
    // 切换批次后强制重挂：curAnim/appliedBatch 与期望不一致时 ApplyClip（见 ApplyAnimation）
    ArtBatch appliedBatch = ArtBatch::Legacy;
    int WeatherSlot() const { return kEnemyWeatherMsgSlotBase + (int)Id(); }
    int SkillMsgSlot() const { return WeatherSlot() + 8; } // 技能预警闪现槽位（与常驻枭王行分开，避免顶掉）
};
