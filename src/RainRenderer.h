#pragma once

#include "IRenderer.h"
#include "scene/RainSystem.h"
#include "VertexArray.h"
#include "VertexBuffer.h"
#include "Math.h"

#include <memory>
#include <vector>

class Shader;
class Sprite;

// 雨滴（实例化绘制）与水花（普通 Sprite）的渲染器。
// 数据源为 RainSystem（纯逻辑），渲染时读取雨滴数据并提交 GPU 绘制。
class RainRenderer : public IRenderer
{
public:
    RainRenderer() = default;
    ~RainRenderer() = default;

    // 绑定数据源并初始化 GL 资源。
    // 需在 GL 上下文就绪、且 "rain" shader 已通过 ShaderLibrary 预编译后调用。
    void Init(std::shared_ptr<RainSystem> rain);

    // 兼容入口：在调用处一次性绘制全部四层（MainScene 已改用 RenderDepthLayer 分层穿插）
    void Render(SpriteRenderer& spriteRenderer) override;

    // ===== 深度分层渲染：雨按 depth 切成四段，穿插到场景的不同渲染层级 =====
    // 层 0 最底层（背景之下）/ 层 1 第一层远景（背景与前景之间）/
    // 层 2 第二层远景（地面之上、障碍之下，含贴地水花）/ 层 3 近景（障碍与蝙蝠之前）
    static constexpr int DepthBandCount = 4;
    static int DepthBandOf(float depth); // depth(0远~1近) -> 0..3
    static float BandAlpha(int band);    // 各层透明度（远景淡、近景实）
    void RenderDepthLayer(SpriteRenderer& spriteRenderer, int band);

private:
    std::shared_ptr<RainSystem> rain_;
    std::shared_ptr<Shader> shader_;

    VertexArray vao_;
    VertexBuffer quadVbo_;
    VertexBuffer instanceVbo_;
    std::vector<RainInstance> instances_;

    Mat4 projection_;
};
