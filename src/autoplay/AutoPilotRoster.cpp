#include "autoplay/AutoPilotDirector.h"
#include "autoplay/policies/ClassicRulePilot.h"
#include <memory>

// =============================================================================
// 内置 Auto Play 策略登记表：新增可自动演示模式的唯一登记点（开闭点）。
// 未登记的模式（限时冲刺/风暴之夜）查不到策略 -> 自动演示不生效，
// 与"怪物未做模式定制"同一设计语义：不是漏配兜底，而是明确的能力边界。
// =============================================================================

void RegisterBuiltinPolicies(AutoPilotDirector& director) {
    // 经典模式：规则式存活策略（障碍/落雷/枭王全威胁模型）
    director.RegisterPolicy(kDefaultModeKey, std::make_shared<ClassicRulePilot>());
}
