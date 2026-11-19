#pragma once
#include <memory>

class UIWidget;

// =============================================================================
// 局内常驻 HUD 绘制：SCORE / 无敌护盾 / 一次性短护盾 / 金币 / 连击 / 限时倒计时。
//
// 纯绘制类，不持有玩法状态：每帧由 MainScene 填一份 Snapshot 快照传入。
// 绘制分两段调用，以保持既有叠放顺序：
//   DrawPrimary  —— SCORE / 无敌护盾 / 金币 / 短护盾（先画，位于天气消息盒之下）
//   DrawOverlay  —— 限时倒计时 / 连击（后画，可压在消息盒之上）
//
// 两套防护分开显示、互不共用槽位：
//   无敌护盾（invincible）—— 闪电图标 + 能量槽（原 HUD，行为不变）
//   一次性短护盾（shield）—— 右侧长条（原俯冲长条的位置），显示 CD/生效剩余
// =============================================================================
class StageHud {
public:
    struct Snapshot {
        // 画布尺寸随快照传入：HUD 不再自己去取全局配置（可直接用任意尺寸构造快照验证布局）
        float screenW = 540.0f;
        float screenH = 960.0f;

        int   score = 0;
        int   coins = 0;

        // ===== 无敌护盾（invincible，原能量护盾）=====
        float energy = 0.0f;              // 充能进度 0..1（满 = 可释放）
        float invincibleTimer = 0.0f;     // 无敌剩余（>0 = 生效中）
        float invincibleDuration = 3.0f;  // 本次无敌总时长（含升级加成）

        // ===== 一次性短护盾（shield）=====
        float shortShieldTimer = 0.0f;         // 生效剩余（0.5s）
        float shortShieldDuration = 0.5f;
        float shortShieldCooldown = 0.0f;      // 释放 CD 剩余
        float shortShieldCooldownTotal = 5.0f;

        float pulseClock = 0.0f;          // HUD 脉冲动画时钟

        float timeLeft = 0.0f;
        bool  timed = false;

        int   comboCount = 0;
        float comboTimer = 0.0f;
        float comboWindow = 6.0f;
        float comboHudY = 60.0f;

        // 入场倒数（Counting 阶段）
        bool  counting = false;
        int   countdownValue = 3;
        float countdownTimer = 0.0f;
        float countdownStep = 0.8f;
    };

    void DrawPrimary(const Snapshot& s, UIWidget& energyIcon);
    void DrawOverlay(const Snapshot& s);
    void DrawCountdown(const Snapshot& s);

    // 无敌护盾图标（闪电）的命中矩形：绘制与 UISystem 命中判定共用一份几何
    static void GetEnergyIconRect(const Snapshot& s, float& cx, float& cy, float& size);

private:
    void DrawScore(const Snapshot& s);
    void DrawEnergy(const Snapshot& s, UIWidget& energyIcon); // 无敌护盾（能量/生效剩余）
    void DrawCoins(const Snapshot& s);
    void DrawShortShield(const Snapshot& s);                  // 一次性短护盾（CD/生效剩余）
    void DrawModeTimer(const Snapshot& s);
    void DrawCombo(const Snapshot& s);
};
