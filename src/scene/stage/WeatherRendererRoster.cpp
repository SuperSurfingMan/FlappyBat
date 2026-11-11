#include "scene/stage/WeatherRendererRoster.h"

#include "scene/stage/WeatherDirector.h"
#include "scene/stage/RainSystem.h"
#include "render/RainRenderer.h"
#include "render/LightningRenderer.h"
#include "render/WindRenderer.h"

#include <memory>

void RegisterBuiltinWeatherRenderers(WeatherDirector& director,
                                     const std::shared_ptr<RainSystem>& rain,
                                     float screenW, float screenH) {
    auto rainRenderer = std::make_unique<RainRenderer>();
    auto lightning = std::make_unique<LightningRenderer>();
    auto wind = std::make_unique<WindRenderer>();

    rainRenderer->Init(rain);
    lightning->Init(rain);
    wind->Init(screenW, screenH);

    // 注册顺序保持与改造前一致；槽位由各渲染器 Slots() 声明，绘制顺序不变
    director.AddRenderer(std::move(rainRenderer));
    director.AddRenderer(std::move(lightning));
    director.AddRenderer(std::move(wind));
}
