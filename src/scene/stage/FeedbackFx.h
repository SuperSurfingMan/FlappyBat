#pragma once
#include "core/Math.h"
#include <string>
#include <vector>

// =============================================================================
// 世界坐标飘字 + 全局抖动：纯表现层，无玩法判定。
//
// 这是 IStageFeedback::FloatTextAt / Shake 的实际落点：障碍、道具、玩家能力、
// 怪物都通过场景的反馈接口写到这里，绘制统一由场景前景层调用。
// 抖动通过平移渲染投影实现，只影响世界 Sprite，不影响 ImGui UI。
// =============================================================================
class FeedbackFx {
public:
    void Reset();

    void Spawn(const std::string& text, Vec2 worldPos,
               float r, float g, float b,
               float fontSize = 20.0f, float life = 1.0f);

    void Update(float dt);
    void Draw() const; // ImGui 前景层

    void Shake(float intensity);
    // 有抖动时输出投影偏移并返回 true；无抖动返回 false
    bool ShakeOffset(float& offX, float& offY) const;

    static constexpr float ShakeDuration = 0.35f;

private:
    struct FloatText {
        std::string text;
        Vec2 worldPos;    // 世界坐标（y 向上）
        float r, g, b;
        float fontSize;
        float life;       // 剩余存活时间
        float totalLife;  // 初始存活时间（用于淡出）
    };

    std::vector<FloatText> floatingTexts;
    float shakeTimer = 0.0f;
    float shakeIntensity = 0.0f;
    float shakeClock = 0.0f;
};
