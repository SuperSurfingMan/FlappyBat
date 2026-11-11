#pragma once
#include <memory>

class WeatherDirector;
class RainSystem;

// 天气渲染器登记表：新增特效 = 实现 IRenderer + 在此加一行注册。
// WeatherDirector 不再 include 任何具体渲染器头文件，也无需为某个渲染器
// 保留专用裸指针（帧数据经 IRenderer::SetGust 统一下行）。
void RegisterBuiltinWeatherRenderers(WeatherDirector& director,
                                     const std::shared_ptr<RainSystem>& rain);
