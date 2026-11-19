#pragma once

// =============================================================================
// 一次性短护盾（shield）/ 弹反规则的唯一事实源。
//
// 注意：本文件与「无敌护盾（invincible）」无关。后者是原能量护盾：
// 能量充满后释放、持续 3.0s + 0.4s/级、期间完全无敌且不因受击消失，
// 规则在 PlayerAbilities（Invincible* 一组接口）内，勿把两者混用。
//
// 本文件只描述短护盾：玩法侧（PlayerAbilities::TryReleaseShortShield）、
// 怪物侧（OwlKing 弹反挂起窗口）、HUD（CD 显示）与场景接线都从这里取常量，
// 避免 0.5s / 5s / 0.1s 散落成魔法数字。
//
// 语义：
//   - 释放后 kDuration 秒内有效；期间挡下任意一次受击即告消失（一次性）；
//   - 释放即把 CD 重置为 kCooldown，CD 归零前不得再次释放（与能量无关）；
//   - 弹反：子弹与蝙蝠在时刻 t 判定碰撞后，若玩家在 [t, t+kParryGrace]
//     内释放短护盾，则额外把该子弹重算轨迹打向枭王。怪物侧把碰撞挂起
//     kParryGrace 秒等待这个释放事件，超时才按正常受击结算。
// =============================================================================
namespace shield {

constexpr float kDuration    = 0.5f;  // 短护盾生效时长（秒）
constexpr float kCooldown    = 5.0f;  // 释放后重置的冷却（秒）
constexpr float kParryGrace  = 0.1f;  // 碰撞后的弹反补救窗口（秒）
constexpr float kBreakInvuln = 0.2f;  // 破盾后的极短免伤，防同帧多伤害源连锁

} // namespace shield
