#pragma once
#include <memory>

class UIWidget;

// =============================================================================
// 局内常驻 HUD 绘制：SCORE / 金币 / 能量护盾 / 俯冲冷却 / 连击 / 限时倒计时。
//
// 纯绘制类，不持有玩法状态：每帧由 MainScene 填一份 Snapshot 快照传入。
// 绘制分两段调用，以保持既有叠放顺序：
//   DrawPrimary  —— SCORE / 能量 / 金币 / 俯冲（先画，位于天气消息盒之下）
//   DrawOverlay  —— 限时倒计时 / 连击（后画，可压在消息盒之上）
// =============================================================================
class StageHud {
public:
    struct Snapshot {
        // 画布尺寸随快照传入：HUD 不再自己去取全局配置（可直接用任意尺寸构造快照验证布局）
        float screenW = 540.0f;
        float screenH = 960.0f;

        int   score = 0;
        int   coins = 0;

        float energy = 0.0f;
        float shieldTimer = 0.0f;
        float shieldDuration = 3.0f;
        float pulseClock = 0.0f;      // HUD 脉冲动画时钟

        float diveCooldown = 0.0f;
        float diveCooldownTotal = 2.5f;
        bool  diving = false;

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

    // 能量图标（闪电）的命中矩形：绘制与 UISystem 命中判定共用一份几何
    static void GetEnergyIconRect(const Snapshot& s, float& cx, float& cy, float& size);

private:
    void DrawScore(const Snapshot& s);
    void DrawEnergy(const Snapshot& s, UIWidget& energyIcon);
    void DrawCoins(const Snapshot& s);
    void DrawDive(const Snapshot& s);
    void DrawModeTimer(const Snapshot& s);
    void DrawCombo(const Snapshot& s);
};
