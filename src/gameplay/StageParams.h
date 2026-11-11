#pragma once
#include "gameplay/ModeDefinition.h"
#include "gameplay/ModeRegistry.h"
#include <cstddef>
#include <string>

// =============================================================================
// 兼容头：第 2 期起，模式定义归 ModeRegistry / ModeDefinition 所有。
// 本头保留是因为 WeatherDirector / RunFlow / StageHud 等仍以它作为「模式参数」
// 的入口；ModeParams 与两个常量的定义已移入 ModeDefinition.h。
//
// ParamsOf(ModeKey) 由 ModeRegistry.h 提供；ParamsOfIndex 便于按注册序取参。
// =============================================================================
const ModeParams& ParamsOfIndex(std::size_t index);

// 启动期完整性校验：遍历注册表，检查每个模式的展示文案与数值参数合法。
bool ValidateStageParams(std::string* firstError = nullptr);
