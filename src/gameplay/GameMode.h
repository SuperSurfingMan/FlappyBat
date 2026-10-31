#pragma once

// =============================================================================
// 多模式枚举（B9）：由 ReadyScene 在进入 MainScene 前设置。
// 独立小头文件：怪物实现（src/enemy/）需要按模式取参，但不允许反向依赖场景头文件
// （docs/怪物系统架构设计.md 决策 D6）；MainScene 内以 using 别名保持
// MainScene::GameMode 的既有引用方式不变。
// =============================================================================
enum class GameMode { Classic, TimeRush, StormNight };
