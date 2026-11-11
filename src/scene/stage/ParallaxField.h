#pragma once
#include "core/Math.h"
#include <memory>
#include <vector>

class Sprite;

// =============================================================================
// 视差背景层（第 4 期从 MainScene 抽出）。
//
// 职责单一：持有背景 / 前景 / 地面三段横向平铺条带，负责载入、初排与无限滚动。
// 它不读配置、不碰分数、不认识模式——速度快慢由调用方每帧传入，
// 因此可脱离场景单独构造与验证（见 tests/parallax_test.cpp）。
//
// 平铺条带的位移量不再手写 270.0f * 3：回绕距离由「单块宽度 × 实际块数」推导，
// 贴图组换成 4 块或别的宽度时行为自动正确。
// =============================================================================
class ParallaxField {
public:
    // 单块贴图宽度（美术资源固定值；回绕距离 = kTileWidth * 块数）
    static constexpr float kTileWidth = 270.0f;

    void Load();   // 从资源管理器取三段贴图组

    // 初排：三段条带按「首块半宽 + i × 块宽」横向铺满，纵向按地面高度对齐
    void Reset(float screenW, float screenH, float terrainHeight, float terrainRenderHeight);

    // 推进滚动：完全移出左侧的块回绕到最右侧
    void Scroll(float dt, float backgroundSpeed, float foregroundSpeed, float terrainSpeed);

    const std::vector<std::shared_ptr<Sprite>>& Background() const { return background_; }
    const std::vector<std::shared_ptr<Sprite>>& Foreground() const { return foreground_; }
    const std::vector<std::shared_ptr<Sprite>>& Terrain() const { return terrain_; }

    bool Loaded() const { return !background_.empty() && !foreground_.empty() && !terrain_.empty(); }
    // 单段的回绕距离（= 块宽 × 块数）；块数为 0 时返回 0，避免除零式空转
    static float WrapDistance(size_t tileCount) { return kTileWidth * (float)tileCount; }

private:
    static void ScrollStrip(std::vector<std::shared_ptr<Sprite>>& strip, float dx);
    static void LayoutStrip(std::vector<std::shared_ptr<Sprite>>& strip, float x0, float y);

    std::vector<std::shared_ptr<Sprite>> background_;
    std::vector<std::shared_ptr<Sprite>> foreground_;
    std::vector<std::shared_ptr<Sprite>> terrain_;
};
