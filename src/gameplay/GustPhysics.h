#pragma once

// =============================================================================
// 阵风推力 -> 蝙蝠附加漂移速度的唯一换算处。
//
// 玩法侧（PlayerAbilities::UpdateMotion）与 AutoPlay 仿真（ClassicRulePilot）
// 共用本系数，保证「预测即所玩」。
//
// 为什么不是直接把推力累加进速度：
//   推力以 px/s² 量级配置（WeatherDirector::GustForceMax=220）。旧实现把它
//   每帧加进垂直速度，但振翅 JumpNow() 会把 velocity 整个覆写成 250，横移
//   SetHorizontalSpeed() 也每帧覆写横速——于是风累积的效果在每次拍翅时清零，
//   且横向分量（左右风）根本没有任何消费点，四向阵风里有一半纯属视觉。
//   改为折算成恒定漂移速度、叠加在玩家操控速度之上：不写入会被覆写的状态，
//   整段阵风持续生效，四向对称，玩家可用反向振翅/横移主动对抗。
// =============================================================================
namespace gust {

// 0.6：经典模式推力 132~220 -> 漂移 79~132 px/s；风暴之夜 ×1.3 -> 103~172 px/s。
// 均低于按键横移 200 px/s 与振翅初速 250 px/s，保证风明显但可对抗。
constexpr float kDriftFactor = 0.6f;

inline float DriftFromForce(float force) { return force * kDriftFactor; }

} // namespace gust
