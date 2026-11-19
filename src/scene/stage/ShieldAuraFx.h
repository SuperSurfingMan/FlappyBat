#pragma once
#include "core/Math.h"

// =============================================================================
// 短护盾的圆形半透明动态遮罩（纯表现，ImGui 前景层）。
//
// 与 FeedbackFx::Draw 同源：世界坐标 -> 屏幕坐标只做 y 翻转，不依赖任何
// 玩法状态。剩余时长由 PlayerAbilities 下发，因此遮罩天然跟随 0.5s 生效期：
// 受击消耗护盾后 remaining 归零，本帧起立即不再绘制。
//
// 尺寸约定：调用方传蝙蝠「碰撞圆半径」（= 体型 * 0.5，与 EnemyContext::batRadius
// 同源）；实际绘制半径由本模块统一加 kAuraPadding（10px），即刚好比蝙蝠大一圈。
// =============================================================================
void DrawShieldAura(float screenH, Vec2 batWorldPos, float batCollisionRadius,
                    float remaining, float duration);
