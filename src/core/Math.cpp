#include "core/Math.h"
#include <memory>
#include <algorithm>


// =========================
// SAT（分离轴定理）辅助（文件内私有）
// =========================

// 把多边形各顶点投影到 axis 上，得到 [min, max] 区间
static void ProjectOntoAxis(
    const std::vector<Vec2>& poly,
    const Vec2& axis,
    float& min,
    float& max)
{
    min = max = poly[0].Dot(axis);

    for (size_t i = 1; i < poly.size(); ++i)
    {
        float p = poly[i].Dot(axis);
        min = std::min(min, p);
        max = std::max(max, p);
    }
}

// 两个多边形在 axis 上的投影区间是否重叠
static bool OverlapOnAxis(
    const std::vector<Vec2>& polyA,
    const std::vector<Vec2>& polyB,
    const Vec2& axis)
{
    float minA, maxA;
    float minB, maxB;

    ProjectOntoAxis(polyA, axis, minA, maxA);
    ProjectOntoAxis(polyB, axis, minB, maxB);

    return !(maxA < minB || maxB < minA);
}


bool CheckCollision(
    const std::vector<SpriteGeometry>& obstaclesGeometry,
    const SpriteGeometry& batGeometry,
    const std::array<Vec2, 3>& obsUVCords,
    const std::array<Vec2, 2>& batUVCords
)
{
    // =========================
    // 1. 构造 bat 实际矩形
    // =========================

    auto batPos   = batGeometry.position;
    auto batScale = batGeometry.scale;

    float batSpriteHalfSize = batScale.x * 0.5; // 蝙蝠贴图半尺寸为32

    std::vector<Vec2> batRect(4);

    float left   = batPos.x - batSpriteHalfSize + batUVCords[0].x * batScale.x;
    float top    = batPos.y - batSpriteHalfSize + batUVCords[0].y * batScale.y;
    float right  = batPos.x - batSpriteHalfSize + batUVCords[1].x * batScale.x;
    float bottom = batPos.y - batSpriteHalfSize + batUVCords[1].y * batScale.y;

    batRect[0] = { left,  bottom }; // 左下
    batRect[1] = { right, bottom }; // 右下
    batRect[2] = { right, top };    // 右上
    batRect[3] = { left,  top };    // 左上


    // =========================
    // 2. 遍历 obstacle
    // =========================

    for (const auto& obstacle : obstaclesGeometry)
    {
        const auto& obsPos   = obstacle.position;
        const auto& obsScale = obstacle.scale;

        std::vector<Vec2> triangle(3);

        for (int i = 0; i < 3; ++i)
        {
            float left = obsPos.x - obsScale.x * 0.5f;
            float bottom = obsPos.y - obsScale.y * 0.5f;

            triangle[i].x = left + obsUVCords[i].x * obsScale.x;
            triangle[i].y = bottom + obsUVCords[i].y * obsScale.y;
        }

        // =========================
        // 3. SAT 轴
        // =========================

        std::vector<Vec2> axes;
        axes.reserve(5);

        // 矩形轴
        axes.push_back({1, 0});
        axes.push_back({0, 1});

        // 三角形边法线
        for (int i = 0; i < 3; ++i)
        {
            Vec2 edge =
            {
                triangle[(i+1)%3].x - triangle[i].x,
                triangle[(i+1)%3].y - triangle[i].y
            };

            Vec2 normal = { -edge.y, edge.x };
            axes.push_back(normal.Normalized());
        }

        // =========================
        // 4. 分离轴检测
        // =========================

        bool separated = false;

        for (const auto& axis : axes)
        {
            if (!OverlapOnAxis(batRect, triangle, axis))
            {
                separated = true;
                break;
            }
        }

        if (!separated)
            return true;
    }

    return false;
}
