#pragma once
#include "gameplay/ModeDefinition.h"
#include "core/Math.h"
#include <memory>

class Sprite;
class SpriteRenderer;
class Config;
class ResourceManager;
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
    bool  batInvincible = false; // 护盾/巨化/开局无敌中
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
    bool diveRequested = false;  // S / ↓：俯冲输入（移动语义由场景实现）
    bool batDiving = false;      // 俯冲急坠中（怪物本体接触判定：俯冲撞击 B6-a）
    bool batParry = false;       // 俯冲无敌帧窗口内（弹反判定用，§4.1 三期）
    // 表现服务：三个角色接口各管一段职责，怪物只取自己用得到的那几个（ISP）。
    // 全部由 MainScene 在构造 ctx 时填充，指向同一个实现对象。
    IFeedbackFx*     fxFx      = nullptr;  // 飘字 / 音效 / 抖动 / 消息盒 / 落雷
    IRunEconomy*     fxEconomy = nullptr;  // 计分 / 金币 / 限时 / 成就
    IPlayerCommands* fxPlayer  = nullptr;  // 致死 / 弹开 / 无敌 / 巨化 / 慢动作
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
    // 建精灵/对象池（纹理已就绪）。配置与资源由导演注入：
    // 怪物不在业务代码里查全局服务，装配关系在 IEnemy 契约上一眼可见。
    virtual void Init(const Config& cfg, ResourceManager& res) = 0;
    virtual void OnRunStart() = 0;    // 重开一局：完全复位
    virtual void OnBatDead() = 0;     // 蝙蝠死亡：即时复位（段落中止）

    // ---- 每帧（反馈经 ctx.fxFx / ctx.fxEconomy / ctx.fxPlayer 同步上行；致死统一 fxPlayer->KillPlayer）----
    virtual void Update(float dt, const EnemyContext& ctx) = 0;

    // ---- 渲染（导演按注册序在固定槽位调用；怪物品牌化 HUD/弹幕自绘）----
    virtual void RenderWorld(SpriteRenderer& renderer) {}
    virtual void RenderUI() {}

    // ---- 规则声明：当前是否处于段落接管及改写了哪些世界规则 ----
    virtual EncounterRules ActiveRules() const = 0;
};
