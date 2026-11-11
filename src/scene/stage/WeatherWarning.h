#pragma once
#include "core/Math.h"

class Config;
class RainSystem;

// =============================================================================
// 天气预警覆盖层（第 5 期从 WeatherDirector 抽出）：落雷预警的屏幕绘制。
//
// 与天气数值逻辑无关，只消费 RainSystem 的只读快照：
//   1) 粗范围矩形——雷可能漂移的整个走廊；
//   2) 精确路径——主分支折线的半透明虚线（实际会劈到的位置）；
//   3) 落点标记——贴地的「雷」字（水平钳制在窗口内）。
// 绘制与命中判定共用同一条折线与同一套收细公式（所见即所判）。
// =============================================================================
namespace weather {

// 点到线段距离（雷线走廊命中判定与虚线绘制共用）
float DistancePointSegment(Vec2 p, Vec2 a, Vec2 b);

// 仅在预警相位绘制；其余相位为空操作。pulseClock 为场景的脉冲时基。
void DrawLightningWarningOverlay(const RainSystem& rain, const Config& cfg, float pulseClock);

} // namespace weather
