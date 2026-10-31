#include "scene/stage/RunFlow.h"
#include <algorithm>

void RunFlow::Reset(const ModeParams& p) {
    score_ = 0;
    lastTier_ = 0;
    comboCount_ = 0;
    comboTimer_ = 0.0f;
    timeLeft_ = p.timeLimit;
    timed_ = p.timed;
    gameOver_ = false;
    collisionEnabled_ = true;
    scoreSaved_ = false;
    introPhase_ = IntroPhase::None; // 具体相位由随后的 OnEnter 决定
    introTimer_ = 0.0f;
    countdownValue_ = 3;
}

void RunFlow::OnEnter(bool maskActive) {
    // 从准备界面"开始游戏"进入时转场遮罩仍在（先揭示后倒数）；
    // 局内重开无遮罩，直接 3-2-1 倒数
    if (maskActive) {
        introPhase_ = IntroPhase::Mask;
    } else {
        introPhase_ = IntroPhase::Counting;
        countdownValue_ = 3;
        introTimer_ = 0.0f;
    }
}

RunFlow::IntroTick RunFlow::TickIntro(float dt, bool maskActive) {
    IntroTick ev;
    if (introPhase_ == IntroPhase::Mask) {
        // 等待准备界面带来的聚焦遮罩完成揭示
        if (!maskActive) {
            introPhase_ = IntroPhase::Counting;
            countdownValue_ = 3;
            introTimer_ = 0.0f;
            ev.becameCounting = true;
        }
        return ev;
    }
    if (introPhase_ != IntroPhase::Counting) return ev;

    introTimer_ += dt;
    if (introTimer_ >= CountdownStepTime) {
        introTimer_ -= CountdownStepTime;
        countdownValue_ -= 1;
        if (countdownValue_ <= 0) {
            introPhase_ = IntroPhase::None;
            ev.finished = true;
        } else {
            ev.countdownAdvanced = true;
        }
    }
    return ev;
}

bool RunFlow::TickTimer(float dt) {
    if (!timed_ || gameOver_ || introPhase_ != IntroPhase::None) return false;
    timeLeft_ -= dt;
    if (timeLeft_ <= 0.0f) {
        timeLeft_ = 0.0f;
        return true;
    }
    return false;
}

void RunFlow::AddScore(size_t base, int scoreFactor) {
    score_ += base * (size_t)scoreFactor;
}

int RunFlow::ApplyPickupCombo(float window, int maxBonus) {
    comboCount_ += 1;
    comboTimer_ = window;
    return std::min(comboCount_ - 1, maxBonus);
}

void RunFlow::TickCombo(float dt) {
    if (comboTimer_ > 0.0f) {
        comboTimer_ = std::max(0.0f, comboTimer_ - dt);
        if (comboTimer_ <= 0.0f) comboCount_ = 0; // 窗口耗尽，连击断掉
    }
}

bool RunFlow::UpdateTier(int& outTier) {
    int tier = Tier();
    if (tier > lastTier_) {
        lastTier_ = tier;
        outTier = tier;
        return true;
    }
    return false;
}

void RunFlow::AddTimeLimit(float sec, float maxTime) {
    if (!timed_) return; // 非限时模式为空操作
    timeLeft_ = std::min(maxTime, timeLeft_ + sec);
}

int RunFlow::Tier() const {
    size_t rawTier = score_ / DifficultyTierSize;
    return (int)std::min(rawTier, (size_t)kMaxDifficultyTier);
}
