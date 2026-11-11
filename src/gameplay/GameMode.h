#pragma once
#include "gameplay/ModeDefinition.h"

// =============================================================================
// 兼容头（第 2 期）：enum class GameMode 已移除。
//
// 模式现在由 gameplay/ModeRegistry.h 的 ModeRegistry + IGameMode 策略管理，
// 标识统一使用 ModeKey（string_view）。保留本头是为了让尚未更新的 include
// 仍能编译；新代码请直接 include ModeDefinition.h / ModeRegistry.h。
// =============================================================================
