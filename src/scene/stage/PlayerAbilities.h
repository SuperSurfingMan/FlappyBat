#pragma once
#include "scene/stage/StageFrame.h"
#include "core/Math.h"
#include <memory>

class BatSprite;
struct IFeedbackFx;

// =============================================================================
// 玩家能力与运动：能量护盾 / 俯冲 / 巨化 / 击杀慢动作 / 蝙蝠每帧运动。
//
// 边界：
//   - 只管"玩家侧状态"；巨化撞飞哪根管道由 ObstacleField 判定（Giant() 提供体型）；
//   - 上行反馈只依赖 IFeedbackFx（飘字/音效/抖动），不直连音频/成就/UI；
//   - locked 由场景在死亡/结算期间置位，阻止护盾释放等主动技能。
// =============================================================================
class PlayerAbilities {
public:
    void Init(std::shared_ptr<BatSprite> bat, IFeedbackFx* fx);
    void Reset();
    void SetLocked(bool locked) { locked_ = locked; }

    // 每帧：阵风外力 -> 俯冲/拍翅/横移 -> 位置积分 -> 护盾/能量计时
    void Update(float dt, const StageFrame& f, Vec2 gustForce);

    // 巨化计时与体型缩放；结束时还原体型与无敌
    void UpdateGiant(float dt);

    void AddEnergy(float amount);
    // freeDuration <= 0 玩家主动释放护盾（能量闸门/护盾计时/HUD/音效）；
    // freeDuration >  0 场景授予的免费无敌（开局保护/调试重开/巨化/俯冲无敌帧），不耗能量、不占护盾 HUD
    void TryReleaseShield(float freeDuration = 0.0f);
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
    float Energy() const { return energy; }
    float ShieldTimer() const { return shieldTimer; }
    float ShieldDuration() const;   // 含升级等级
    float PulseClock() const { return pulseClock; }
    float DiveCooldown() const { return diveCooldown; }
    float DiveCooldownTotal() const { return DiveCooldownTime; }
    bool  Diving() const { return diveTimer > 0.0f; }
    bool  DiveParryWindow() const { return diveIframe > 0.0f; }
    bool  IsGiant() const { return giantTimer > 0.0f; }
    bool  IsInvincible() const;
    float TimeScale() const;

private:
    void UpdateMotion(float dt, const StageFrame& f, Vec2 gustForce);

    std::shared_ptr<BatSprite> bat;
    IFeedbackFx* fx = nullptr;
    bool locked_ = false;

    // ===== 能量护盾 =====
    static constexpr float ShieldBaseDuration = 3.0f;     // 基础持续时间
    static constexpr float ShieldDurationPerLevel = 0.4f; // 每级 +0.4s（满级 5 级 -> 5.0s）
    static constexpr float ChargeMultPerLevel = 0.1f;     // 每级充能速度 +10%（满级 ×1.5）
    float energy = 0.0f;        // 0.0 ~ 1.0
    float shieldTimer = 0.0f;   // 无敌剩余时间（>0 表示护盾生效中）
    float pulseClock = 0.0f;    // HUD 动画计时

    // ===== 俯冲（S/↓ 定速急坠 0.18s，全程无敌帧，冷却 2.5s）=====
    static constexpr float DiveDuration = 0.18f;
    static constexpr float DiveSpeed = 1000.0f;
    static constexpr float DiveInvulnDur = 0.4f;
    static constexpr float DiveCooldownTime = 2.5f;
    float diveTimer = 0.0f;
    float diveIframe = 0.0f;
    float diveCooldown = 0.0f;

    // ===== 巨型蝙蝠 =====
    static constexpr float GiantDuration = 5.0f;
    static constexpr float GiantScaleFactor = 2.0f;
    float giantTimer = 0.0f;

    // ===== 击杀慢动作 =====
    static constexpr float SlowMoScale = 0.35f;
    static constexpr float SlowMoDur = 0.7f;
    float slowMoTimer = 0.0f;
};
