#pragma once
#include "scene/stage/StageFrame.h"
#include "core/Math.h"
#include <memory>

class BatSprite;
class Config;
class PlayerProfile;
class TuningParams;
struct IFeedbackFx;

// =============================================================================
// 玩家能力与运动：无敌护盾（invincible）/ 一次性短护盾（shield）/ 巨化 /
// 击杀慢动作 / 蝙蝠每帧运动。
//
// 两套防护彼此独立，语义不可混用：
//   - invincible：原「能量护盾」。能量充满后按空格（或点 HUD 闪电图标）释放，
//     持续 3.0s + 0.4s/级，期间完全无敌，且不会因受击而提前消失。
//     空格只承担释放职责，不再参与振翅；振翅用鼠标左键 / W / ↑。
//     走 BatSprite 的无敌计时器（SetInvincible）。
//   - shield：上一轮新增的一次性短护盾。不看能量、固定 0.5s、CD 5s，
//     挡下任意一次受击立即消失；碰撞后 0.1s 内补放可触发弹反。
//     规则常量集中在 gameplay/ShieldRules.h。
//
// 边界：
//   - 只管"玩家侧状态"；巨化撞飞哪根管道由 ObstacleField 判定（Giant() 提供体型）；
//   - 上行反馈只依赖 IFeedbackFx（飘字/音效/抖动），不直连音频/成就/UI；
//   - locked 由场景在死亡/结算期间置位，阻止护盾释放等主动技能。
// =============================================================================
class PlayerAbilities {
public:
    // 依赖注入：配置（体型/重力边界）、玩家档案（金币/升级）与调参
    // （巨化无敌时长等设置页可改参数）。
    void Init(std::shared_ptr<BatSprite> bat, IFeedbackFx* fx,
              const Config& cfg, PlayerProfile& profile, const TuningParams& tuning);
    void Reset();
    void SetLocked(bool locked) { locked_ = locked; }

    // 每帧：无敌护盾计时 -> 短护盾计时 -> 阵风漂移/拍翅/横移 -> 位置积分
    void Update(float dt, const StageFrame& f, Vec2 gustForce);

    // 巨化计时与体型缩放；结束时还原体型与无敌
    void UpdateGiant(float dt);

    // ===== 无敌护盾（invincible，原能量护盾；行为与旧版一致）=====
    // 道具能量：只给无敌护盾充能，充满（1.0）才能释放。
    void AddInvincibleEnergy(float amount);
    // 玩家主动释放（空格 / HUD 闪电图标 / Auto Play）：能量满、未生效、未锁定时可用。
    void TryReleaseInvincible();
    // 场景授予的免费无敌（开局保护/调试重开/巨化）：不消耗能量、不占 HUD 的护盾计时。
    void GrantInvincible(float dur);
    float InvincibleEnergy() const { return invincibleEnergy; }
    float InvincibleTimer() const { return invincibleTimer; }
    float InvincibleDuration() const;   // 3.0s + 0.4s/级（升级轨道）
    bool  InvincibleActive() const { return invincibleTimer > 0.0f; }
    bool  InvincibleReady() const;      // 能量满且当前可释放

    // ===== 一次性短护盾（shield，CD 制；弹反用）=====
    // 释放短护盾（S/↓ / Auto Play）：无能量门槛，CD 归零、未生效、未巨化、未锁定时可释放，
    // 释放即把 CD 重置为 5s。
    void TryReleaseShortShield();
    // 短护盾挡下一次受击：成功则护盾立即消失（CD 不重置），返回 true。
    // 由场景碰撞分支与怪物 DamageBat 统一调用，保证"一次受击后消失"。
    bool ConsumeShortShield();
    // 释放后 kParryGrace 内为真：怪物侧据此把挂起的子弹碰撞判为弹反。
    bool ParryReleaseWindow() const;
    float ShortShieldTimer() const { return shortShieldTimer; }
    float ShortShieldDuration() const;      // 固定 0.5s（ShieldRules.h）
    float ShortShieldCooldown() const { return shortShieldCooldown; }
    float ShortShieldCooldownTotal() const; // 固定 5s（ShieldRules.h）
    bool  ShortShieldActive() const { return shortShieldTimer > 0.0f; }
    bool  ShortShieldReady() const;

    void GrantInvuln(float dur);   // 受击无敌（不缩短既有更长无敌）
    void TriggerSlowMo(float dur);
    void TickSlowMo(float dt); // 以真实 dt 递减慢动作计时（在 TimeScale 之前）
    void Bounce(float dx, float dy);

    void ActivateGiant();
    void EndGiant();

    struct GiantBody {
        bool active = false;
        Vec2 center{};
        float halfSize = 0.0f;
    };
    GiantBody Giant() const;

    // ===== 只读状态（HUD / 怪物上下文 / 碰撞 / 道具）=====
    std::shared_ptr<BatSprite> Bat() const { return bat; }
    float PulseClock() const { return pulseClock; }
    bool  IsGiant() const { return giantTimer > 0.0f; }
    // 聚合判定：蝙蝠自身无敌（无敌护盾/受击无敌/开局保护/巨化）+ 一次性短护盾
    bool  IsInvincible() const;
    // 除一次性短护盾之外的无敌来源。伤害闸门据此排序：
    // 原有无敌优先直接吸收，不应顺手把短护盾消耗掉。
    bool  HasOtherInvincibility() const;
    float TimeScale() const;

private:
    void UpdateMotion(float dt, const StageFrame& f, Vec2 gustForce);

    std::shared_ptr<BatSprite> bat;
    IFeedbackFx* fx = nullptr;
    const Config* cfg_ = nullptr;
    PlayerProfile* profile_ = nullptr;
    const TuningParams* tuning_ = nullptr;
    bool locked_ = false;

    // ===== 无敌护盾（invincible，原能量护盾）=====
    static constexpr float InvincibleBaseDuration = 3.0f;      // 基础持续时间
    static constexpr float InvincibleDurationPerLevel = 0.4f;  // 每级 +0.4s（满级 5 级 -> 5.0s）
    static constexpr float InvincibleChargePerLevel = 0.1f;    // 每级充能速度 +10%（满级 ×1.5）
    float invincibleEnergy = 0.0f; // 0.0 ~ 1.0（充满才能释放）
    float invincibleTimer = 0.0f;  // 无敌护盾剩余时间（>0 表示护盾生效中）

    // ===== 一次性短护盾（规则常量见 gameplay/ShieldRules.h）=====
    float shortShieldTimer = 0.0f;    // 生效剩余（>0 时挡下一次受击）
    float shortShieldCooldown = 0.0f; // 释放 CD 剩余
    float parryReleaseAge = -1.0f;    // 距上次释放的秒数；<0 = 无释放事件

    float pulseClock = 0.0f; // HUD 动画计时

    // ===== 巨型蝙蝠 =====
    // 无敌时长不在此处写常量：由设置页可调的 TuningParams::GiantInvulnSec() 给出
    static constexpr float GiantScaleFactor = 2.0f;
    float giantTimer = 0.0f;

    // ===== 击杀慢动作 =====
    static constexpr float SlowMoScale = 0.35f;
    static constexpr float SlowMoDur = 0.7f;
    float slowMoTimer = 0.0f;
};
