#pragma once
// =============================================================================
// 圆形聚焦转场（ReadyScene -> MainScene 专用）—— ISceneTransition 的首个实现。
// 聚焦：孔径自全开收缩到恰好框住中央蝙蝠（ease-in 立方：缓慢起步、加速吸入）
// 保持：孔径轻微呼吸抖动（切幕窗口；换景由 SceneManager 在 ReadyToSwitch 时执行，
//       本对象由 SceneManager 持有，状态天然跨场景延续）
// 揭示：孔径弹性扩张至全开（ease-out back：爆发过冲后回弹，与聚焦刻意不对称）
// 绘制走 ImGui 前景层：孔外纯黑环形三角扇 + 孔缘琥珀光环
// =============================================================================
#include "ISceneTransition.h"
#include "imgui.h"
#include "../ui/UITheme.h"
#include <cmath>
#include <algorithm>

class CircleFocusTransition : public ISceneTransition {
public:
    void Start(const TransitionContext& ctx) override {
        if (phase != Phase::None) return; // 已在转场中，忽略重复触发
        phase = Phase::Focus;
        center = ImVec2(ctx.focusAnchor.x, ctx.focusAnchor.y);
        timer = 0.0f;
        holeRadius = MaxRadius;
    }

    void Update(float dt) override {
        if (phase == Phase::None) return;
        timer += dt;
        clock += dt;

        switch (phase) {
        case Phase::Focus:
            if (timer >= FocusDuration) {
                timer = 0.0f;
                phase = Phase::Hold;
                holeRadius = FocusRadius;
            } else {
                float t = timer / FocusDuration;
                float e = t * t * t; // ease-in cubic：加速吸入
                holeRadius = MaxRadius + (FocusRadius - MaxRadius) * e;
            }
            break;
        case Phase::Hold:
            // 呼吸微抖蓄力；此阶段内完成场景切换（SceneManager 于 ReadyToSwitch 时换景）
            holeRadius = FocusRadius + std::sin(clock * 18.0f) * 2.5f;
            if (timer >= HoldDuration) {
                timer = 0.0f;
                phase = Phase::Reveal;
            }
            break;
        case Phase::Reveal: {
            float t = std::clamp(timer / RevealDuration, 0.0f, 1.0f);
            // ease-out back：爆发式张开并过冲，随后回弹收尾
            holeRadius = FocusRadius + (MaxRadius * 1.15f - FocusRadius) * EaseOutBack(t);
            if (timer >= RevealDuration) {
                phase = Phase::None; // 揭示完成，转场结束
            }
            break;
        }
        default:
            break;
        }
    }

    // ImGui 前景层绘制：环形三角扇铺满孔外全部区域（纯黑）+ 孔缘光环
    void Draw() override {
        if (phase == Phase::None) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        const float outerR = 900.0f; // 大于屏幕半对角线(约550)，覆盖四角
        const int segments = 96;
        const float tau = 6.2831853f; // 2*PI
        const ImU32 black = IM_COL32(0, 0, 0, 255);
        for (int i = 0; i < segments; ++i) {
            float a0 = tau * (float)i / segments;
            float a1 = tau * (float)(i + 1) / segments;
            ImVec2 o0(center.x + std::cos(a0) * outerR, center.y + std::sin(a0) * outerR);
            ImVec2 o1(center.x + std::cos(a1) * outerR, center.y + std::sin(a1) * outerR);
            ImVec2 i0(center.x + std::cos(a0) * holeRadius, center.y + std::sin(a0) * holeRadius);
            ImVec2 i1(center.x + std::cos(a1) * holeRadius, center.y + std::sin(a1) * holeRadius);
            dl->AddTriangleFilled(o0, o1, i1, black);
            dl->AddTriangleFilled(o0, i1, i0, black);
        }

        // 孔缘琥珀光环：聚焦时脉冲渐强，保持时高频闪烁蓄力，揭示时衰减消失
        float ringAlpha = 0.0f;
        float ringWidth = 1.0f;
        if (phase == Phase::Focus) {
            float t = timer / FocusDuration;
            ringAlpha = 0.35f + 0.60f * t;
            ringWidth = 2.5f + 3.5f * t + 2.0f * std::abs(std::sin(clock * 22.0f));
        } else if (phase == Phase::Hold) {
            ringAlpha = 1.0f;
            ringWidth = 5.0f + 3.0f * std::abs(std::sin(clock * 26.0f));
        } else if (phase == Phase::Reveal) {
            float t = std::clamp(timer / RevealDuration, 0.0f, 1.0f);
            ringAlpha = 1.0f - t;
            ringWidth = 6.0f * (1.0f - t) + 1.5f;
        }
        if (ringAlpha > 0.02f) {
            dl->AddCircle(center, std::max(holeRadius, 1.0f),
                          UITheme::ToImU32(UITheme::Amber, ringAlpha), segments, ringWidth);
        }
    }

    bool ReadyToSwitch() const override { return phase == Phase::Hold; } // 聚焦到位，可以切场景
    bool IsFinished() const override { return phase == Phase::None; }    // 揭示完成，可拆除覆盖层
    bool BlocksInteraction() const override { return phase != Phase::None; }

private:
    static float EaseOutBack(float t) {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u; // 末端过冲回弹
    }

    enum class Phase { None, Focus, Hold, Reveal };
    Phase phase = Phase::None;
    ImVec2 center = ImVec2(0.0f, 0.0f);
    float timer = 0.0f;   // 当前阶段计时
    float clock = 0.0f;   // 全局时钟（光环脉冲）
    float holeRadius = 0.0f;

    static constexpr float MaxRadius = 620.0f;     // 全开孔径（大于半屏对角线）
    static constexpr float FocusRadius = 105.0f;   // 聚焦终点：恰好框住中央蝙蝠(64px)
    static constexpr float FocusDuration = 0.95f;  // 聚焦时长
    static constexpr float HoldDuration = 0.50f;   // 保持时长（切幕窗口）
    static constexpr float RevealDuration = 0.70f; // 揭示时长
};
