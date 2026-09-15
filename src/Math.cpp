#include "Math.h"
#include <memory>
#include "Point2D.h"


bool CheckCollision(
    const std::vector<SpriteGeometry>& obstaclesGeometry,
    const SpriteGeometry& batGeometry,
    const std::array<Point2D, 3>& obsUVCords,
    const std::array<Point2D, 2>& batUVCords
)
{
    // =========================
    // 1. 构造 bat 实际矩形
    // =========================

    auto batPos   = batGeometry.position;
    auto batScale = batGeometry.scale;

    float batSpriteHalfSize = batScale.x * 0.5; // 蝙蝠贴图半尺寸为32

    std::vector<Point2D> batRect(4);

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

        std::vector<Point2D> triangle(3);

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

        std::vector<Point2D> axes;
        axes.reserve(5);

        // 矩形轴
        axes.push_back({1, 0});
        axes.push_back({0, 1});

        // 三角形边法线
        for (int i = 0; i < 3; ++i)
        {
            Point2D edge =
            {
                triangle[(i+1)%3].x - triangle[i].x,
                triangle[(i+1)%3].y - triangle[i].y
            };

            Point2D normal = { -edge.y, edge.x };
            axes.push_back(Normalize(normal));
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