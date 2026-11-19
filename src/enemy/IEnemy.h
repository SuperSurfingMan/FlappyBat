#pragma once
#include "gameplay/ModeDefinition.h"
#include "core/Math.h"
#include <memory>
#include <vector>

class Sprite;
class SpriteRenderer;
class Config;
class ResourceManager;
class TuningParams;      // 设置页可改的玩法参数（血量倍率等）
struct IFeedbackFx;      // 角色接口：纯表现
struct IRunEconomy;      // 角色接口：结算通道
struct IPlayerCommands;  // 角色接口：玩家权威操作

// =============================================================================
// 怪物框架公共契约（设计：docs/怪物系统架构设计.md §4）
// 血月枭王为首个实现（src/enemy/owlking/）；新增怪物按 §9 手册接入：
// 实现本接口 + EnemyRoster 登记一行，场景与导演零改动。
// =============================================================================

enum class EnemyId {
    OwlKing
    // , SpiderQueen, ... 新怪物在此追加枚举值
};

// 怪物天气消息盒槽位基址：场景自留 0~2（阵风/落雷/风暴档位），
// 怪物按 kEnemyWeatherMsgSlotBase + (int)EnemyId 自留段位，互不踩踏
constexpr int kEnemyWeatherMsgSlotBase = 3;

// ---- 每帧下行上下文：世界快照 + 输入意图 + 表现服务（怪物只读，仅当帧有效） ----
struct EnemyContext {
    // 蝙蝠
    Vec2  batPos{};
    float batRadius = 0.f;       // 等效碰撞半径（MainScene 由蝙蝠尺寸推导填充）
    bool  batInvincible = false; // 任意无敌中（无敌护盾/巨化/开局保护/受击无敌/短护盾，聚合值）
    bool  batOtherInvincible = false; // 除一次性短护盾外的无敌（伤害闸门优先吸收，不碎短护盾）
    bool  batGiant = false;      // 巨化期（未来「巨化冲撞对怪节拍扣血」用）
    // 局面
    size_t score = 0;
    float  timeLeft = -1.f;      // 限时模式剩余秒数；<0 = 非限时
    ModeKey mode = kDefaultModeKey;    // 怪物按此自取调参（TuningFor(ModeKey)）
    // 世界几何（怪物不直连 Config）
    float terrainY = 0.f;
    float screenW = 0.f;
    float screenH = 0.f;
    // 输入意图（MainScene 统一采集；怪物按需消费，无消费者则空转）
    bool firePrimary = false;    // 右键 / F 键：主攻击（枭王=音爆）
    // 短护盾 / 弹反：碰撞在 t 判定后由怪物挂起 kParryGrace 秒，
    // 期间玩家释放短护盾（parryArmed）即触发弹反，见 gameplay/ShieldRules.h
    bool batShieldActive = false; // 短护盾生效中（挡下一次受击即消失）
    bool parryArmed = false;      // 短护盾刚释放，处于弹反补救窗口内
    // 表现服务：三个角色接口各管一段职责，怪物只取自己用得到的那几个（ISP）。
    // 全部由 MainScene 在构造 ctx 时填充，指向同一个实现对象。
    IFeedbackFx*     fxFx      = nullptr;  // 飘字 / 音效 / 抖动 / 消息盒 / 落雷
    IRunEconomy*     fxEconomy = nullptr;  // 计分 / 金币 / 限时 / 成就
    IPlayerCommands* fxPlayer  = nullptr;  // 致死 / 弹开 / 无敌 / 巨化 / 慢动作
};

// ---- 上行威胁快照：与 EnemyContext 的下行方向相对，供 AutoPlay 等观察者只读 ----
// 怪物不主动推送：观察者经 EnemyDirector::CaptureSense 聚合拉取，
// 未实现 CaptureSense 的怪物保持 present=false（= 对自动演示不可见，不参与其决策）。
// 枚举是"可观察契约"，与具体怪物内部的私有枚举按值对应；新增怪物追加字段即扩展，
// 观察者只读自己认识的字段，忽略未知字段。
enum class EnemySensePhase { Hidden, Warning, Engagement, Defeat, Leave };
enum class EnemySenseSkill {
    None, FeatherStorm, MoonGaze, ShadowDash, ScreechWave, StormCall, BloodRain
};
enum class EnemySenseSkillState { Idle, Telegraph, Active, Fatigue };

struct EnemySense {
    bool present = false;               // 是否有怪物填充了本快照
    EnemySensePhase phase = EnemySensePhase::Hidden;
    EnemySenseSkill skill = EnemySenseSkill::None;
    EnemySenseSkillState skillState = EnemySenseSkillState::Idle;
    float skillTimer = 0.f;             // 当前 SkillState 已进行时间
    float skillStateDur = 0.f;          // 当前 SkillState 的总时长（进度 = timer/dur）
    // 本体
    float bossY = 480.f;
    int   batHearts = 3;                // 蝙蝠剩余心数（3 心模型怪物才有意义）
    int   sonicCharge = 0;              // 音爆充能段数（>0 时可 firePrimary）
    // 带状威胁（Telegraph 起手即定，观察者按当前几何+逐帧重规划躲避）
    float gazeY = 0.f;                  // B2 光带中心 y（配 gazeDir 判断扫掠去向）
    float gazeDir = 0.f;                // B2 扫掠方向：-1 下 / +1 上
    float shadowBandY = 0.f;            // B3 暗影带中心 y（预警开始时锚定的蝙蝠高度）
    // 点/弧威胁
    struct Wave {
        bool alive = false;
        float radius = 0.f;   // 当前弧半径（扩张中，配 speed 外推未来半径）
        float speed = 0.f;    // 扩张速度 px/s
        float gapY = 0.f;     // 缺口中心 y（缺口内穿越豁免）
        float gapHalf = 0.f;  // 缺口半高
        float centerY = 0.f;  // 弧心 y
    };
    std::vector<Wave> waves;            // B4 音波弧（至多 3）
    float arcCenterX = 0.f;             // B4 弧心 x（弧仅向该点左半场扩散）
    struct Projectile {
        bool alive = false;
        Vec2 pos{};
        Vec2 vel{};                     // 羽刃有效；坠羽恒速下落（vel.y < 0）
        bool hasVel = false;            // false = 观察者按静止点处理
    };
    std::vector<Projectile> projectiles; // B1 羽刃 + B6 坠羽（点弹体）
    // B5 雷暴三落点（Active 起有效；落点走廊 = 脚本雷，雷体经落雷管线可见）
    float stormXs[3] = { 0.f, 0.f, 0.f };
    bool  stormArmed = false;
    // B6 顶部标记列（当前批；0.5s 换批，观察者逐帧重读即可）
    float bloodCols[4] = { 0.f, 0.f, 0.f, 0.f };
    bool  bloodColsActive = false;
};

// ---- 段落规则接管：怪物声明「当前改写了哪些世界规则」，场景只查聚合结果 ----
struct EncounterRules {
    bool  suppressPipeSpawn = false;  // 停止新管道列生成（段落化，BOSS设计文档 §3）
    bool  freezeOffscreenPipes = false; // 尚未入画的管道冻结在屏外，不再从右缘滚入（视野内残管自然滚出）
    bool  suppressItemSpawn = false;  // 停止走廊道具生成（怪物道具接管补给）
    bool  pipesLethal = true;         // false = 管道不即死（怪物有专属受击模型）
    bool  edgesLethal = true;         // false = 撞地/顶交怪物处理（弹起扣心）
    bool  pauseGust = false;          // 段落期阵风暂停（BOSS设计文档 §9，二期启用）
    float scrimBoost = 0.f;           // 天色额外压暗（BOSS设计文档 §9：+0.10，二期启用）
    float minRainIntensity = 0.f;     // 段落期雨强下限（BOSS设计文档 §9：遭遇期抬到 ≥80%；0 = 不接管）

    bool Active() const { // 是否处于段落接管（任一规则被改写）
        return suppressPipeSpawn || freezeOffscreenPipes || suppressItemSpawn ||
               !pipesLethal || !edgesLethal || pauseGust || scrimBoost > 0.f ||
               minRainIntensity > 0.f;
    }
};

// ---- 怪物接口：生命周期/每帧/渲染/规则声明 ----
class IEnemy {
public:
    virtual ~IEnemy() = default;

    virtual EnemyId Id() const = 0;

    // ---- 生命周期（导演统一驱动；暂停/入场冻结由场景门控 Update 调用保证）----
    // 建精灵/对象池（纹理已就绪）。配置、资源与调参由导演注入：
    // 怪物不在业务代码里查全局服务，装配关系在 IEnemy 契约上一眼可见。
    virtual void Init(const Config& cfg, ResourceManager& res, const TuningParams& tuning) = 0;
    virtual void OnRunStart() = 0;    // 重开一局：完全复位
    virtual void OnBatDead() = 0;     // 蝙蝠死亡：即时复位（段落中止）

    // ---- 每帧（反馈经 ctx.fxFx / ctx.fxEconomy / ctx.fxPlayer 同步上行；致死统一 fxPlayer->KillPlayer）----
    virtual void Update(float dt, const EnemyContext& ctx) = 0;

    // ---- 渲染（导演按注册序在固定槽位调用；怪物品牌化 HUD/弹幕自绘）----
    virtual void RenderWorld(SpriteRenderer& renderer) {}
    virtual void RenderUI() {}

    // ---- 规则声明：当前是否处于段落接管及改写了哪些世界规则 ----
    virtual EncounterRules ActiveRules() const = 0;

    // ---- 威胁快照（可选）：把当前威胁几何写给观察者（AutoPlay 等）----
    // 默认空实现 = 本怪物对自动演示不可见；实现方只填自己的字段并置 present=true
    virtual void CaptureSense(EnemySense& out) const { (void)out; }
};
