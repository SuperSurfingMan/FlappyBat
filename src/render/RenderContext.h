#pragma once

#include "core/Math.h"

class SpriteRenderer;

// =============================================================================
// 一帧内的渲染上下文(方案一):贯穿所有渲染器的共享只读信息。
// 投影由场景每帧计算(可能含全局抖动偏移)后统一分发,专门渲染器不再各自
// 在 Init 里缓存 Ortho——世界内所有元素(含雨/风/闪电)随抖动一致震动,
// ImGui UI 不受影响。普通 Sprite 仍统一经 ctx.sprites(SpriteRenderer)绘制。
// =============================================================================
struct RenderContext {
    SpriteRenderer& sprites;   // 普通 Sprite 的唯一绘制出口
    const Mat4& projection;    // 本帧世界投影(已含抖动偏移,若有)
};
