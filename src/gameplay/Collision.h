#pragma once
// =============================================================================
// 碰撞检测（gameplay 侧）：SAT 分离轴检测蝙蝠矩形与障碍贴图 UV 三角形。
// 输入几何由调用方构造（ObstacleField），UV 碰撞坐标来自 ResourceManager 注册表；
// 本模块只做纯几何判定，不依赖场景与资源。
// =============================================================================
#include "core/Math.h"
#include <array>
#include <vector>

// 精灵几何（世界坐标）：碰撞检测的输入
struct SpriteGeometry {
    Vec2 scale;
    Vec2 position;
};

bool CheckCollision(
    const std::vector<SpriteGeometry>& obstaclesGeometry,
    const SpriteGeometry& batGeometry,
    const std::array<Vec2, 3>& obsUVCords,
    const std::array<Vec2, 2>& batUVCords
);
