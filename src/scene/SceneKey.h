#pragma once
#include <string>

// =============================================================================
// 场景标识（第 4 期）：从封闭枚举改为开放字符串键。
//
// 改之前：新增一个场景要改 SceneId 枚举，而枚举属于「所有人都要重新编译的
// 中心头文件」——这正是开闭原则要消掉的那种改动。现在新增场景 =
// 组合根 Register(kSceneYourScene, 实例) 一行，无需改任何中心定义。
//
// 键的取值是稳定的字符串字面量（用于日志与注册表查找），
// 命名常量集中在本文件，避免各处手写字符串拼错。
// 未注册的键在 RequestScene 处被拒绝并打印错误，不会静默走错场景。
// =============================================================================
using SceneKey = std::string;

inline constexpr const char* kSceneBoot         = "boot";
inline constexpr const char* kSceneReady        = "ready";
inline constexpr const char* kSceneMain         = "main";
inline constexpr const char* kSceneScoreRank    = "scoreRank";
inline constexpr const char* kSceneAchievements = "achievements";
inline constexpr const char* kSceneSettings     = "settings";
