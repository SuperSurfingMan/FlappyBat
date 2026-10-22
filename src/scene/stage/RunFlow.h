#pragma once
#include "../../GameMode.h"
#include "../../StageParams.h"
#include <cstddef>

// =============================================================================
// 一局的流程与统计：模式 / 难度档位 / 分数 / 连击窗口 / 限时 / 入场倒数 /
// 结算存档标志 / 死亡与碰撞门控。
//
// 边界：
//   - 只持数据与纯规则，不播音效、不画飘字、不写排行榜；
//   - 副作用由 MainScene 根据 TickIntro/TickTimer/UpdateTier 的返回值编排；
//   - 分数写入统一入口 AddScore，模式倍率由调用方传入。
// =============================================================================
class RunFlow {
public:
    enum class IntroPhase { None, Mask, Counting };

    void SetMode(GameMode m) { mode_ = m; }
    GameMode Mode() const { return mode_; }

    void Reset(const ModeParams& p);
    void OnEnter(bool maskActive);

    struct IntroTick {
        bool becameCounting = false;   // 遮罩揭示完毕，进入 3-2-1
        bool countdownAdvanced = false; // 倒数换到下一个数字
        bool finished = false;          // 倒数结束，正式开局
    };
    IntroTick TickIntro(float dt, bool maskActive);

    // 限时模式倒计时；返回 true = 本帧耗尽（调用方执行结算）
    bool TickTimer(float dt);

    void AddScore(size_t base, int scoreFactor);
    int  ApplyPickupCombo(float window, int maxBonus); // 返回本次额外加分
    void TickCombo(float dt);
    bool UpdateTier(int& outTier); // 返回 true = 本帧跨档

    void AddTimeLimit(float sec, float maxTime);

    // ===== 只读状态 =====
    size_t Score() const { return score_; }
    int Tier() const;
    float TimeLeft() const { return timeLeft_; }
    bool Timed() const { return timed_; }
    int ComboCount() const { return comboCount_; }
    float ComboTimer() const { return comboTimer_; }

    bool IsGameOver() const { return gameOver_; }
    void SetGameOver(bool v) { gameOver_ = v; }
    bool CollisionEnabled() const { return collisionEnabled_; }
    void SetCollisionEnabled(bool v) { collisionEnabled_ = v; }
    bool ScoreSaved() const { return scoreSaved_; }
    void MarkScoreSaved() { scoreSaved_ = true; }

    IntroPhase Phase() const { return introPhase_; }
    bool InIntro() const { return introPhase_ != IntroPhase::None; }
    bool MaskPhase() const { return introPhase_ == IntroPhase::Mask; }
    bool Counting() const { return introPhase_ == IntroPhase::Counting; }
    int CountdownValue() const { return countdownValue_; }
    float CountdownTimer() const { return introTimer_; }

    static constexpr float CountdownStepTime = 0.8f;
    static constexpr int   DifficultyTierSize = 10;
    static constexpr float SpeedBonusPerTier = 0.08f;
    static constexpr float GapShrinkPerTier = 0.03f;
    static constexpr float ComboWindow = 6.0f;
    static constexpr int   ComboMaxBonus = 3;

private:
    GameMode mode_ = GameMode::Classic;
    size_t score_ = 0;
    int lastTier_ = 0;
    float timeLeft_ = kTimeRushDuration;
    bool timed_ = false;
    int comboCount_ = 0;
    float comboTimer_ = 0.0f;
    bool gameOver_ = false;
    bool collisionEnabled_ = true;
    bool scoreSaved_ = false;
    IntroPhase introPhase_ = IntroPhase::None;
    float introTimer_ = 0.0f;
    int countdownValue_ = 3;
};
