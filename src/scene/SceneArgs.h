#pragma once
// =============================================================================
// 切场启动参数包：场景只声明意图（SceneId + SceneArgs），不再 include 目标场景
// 头文件、更不再 static_pointer_cast 下转设参。字段按需增补，非目标场景自行忽略。
// =============================================================================
#include "../GameMode.h"
#include "../Point2D.h"
#include <optional>

struct SceneArgs {
    // 进入主场景的对局模式（经 Scene::OnPrepare 在 Reset 前注入）
    GameMode gameMode = GameMode::Classic;

    // 转场聚焦锚点（屏幕坐标，Y 向下）：由发起方随请求携带（如 ReadyScene 的蝙蝠位置，
    // 精灵世界坐标 Y 向上需先做 SH - y 翻转）；空则由转场自选默认锚点（屏幕中心）
    std::optional<Vec2> focusAnchorScreen;
};
