#pragma once

#include "render/IRenderer.h"
#include "render/InstancedQuadRenderer.h"
#include "scene/stage/RainSystem.h"

#include <memory>
#include <vector>

class Shader;

// 雨滴(实例化绘制)与水花(普通 Sprite)的渲染器。
// 数据源为 RainSystem(纯逻辑),渲染时读取雨滴数据并提交 GPU 绘制。
// 认领 4 个雨带槽位(RainFar/RainMid/RainGround/RainNear),按 depth 分层
// 穿插到场景纵深——层序由 WorldSlot 管线表达,不再依赖调用处的调用顺序。
class RainRenderer : public InstancedQuadRenderer
{
public:
    RainRenderer() = default;
    ~RainRenderer() = default;

    // 绑定数据源并初始化 GL 资源。
    // 需在 GL 上下文就绪、且 "rain" shader 已通过 ShaderLibrary 预编译后调用。
    void Init(std::shared_ptr<RainSystem> rain);

    // 只响应认领的雨带槽位;其余槽位调用为空操作
    void Render(const RenderContext& ctx, WorldSlot slot) override;

    std::vector<WorldSlot> Slots() const override {
        return { WorldSlot::RainFar, WorldSlot::RainMid,
                 WorldSlot::RainGround, WorldSlot::RainNear };
    }

    // ===== 深度分层:雨按 depth 切成四段,穿插到场景的不同渲染层级 =====
    // 层 0 最底层(背景之下)/ 层 1 第一层远景(背景与前景之间)/
    // 层 2 第二层远景(地面之上、障碍之下,含贴地水花)/ 层 3 近景(障碍与蝙蝠之前)
    static constexpr int DepthBandCount = 4;
    static int DepthBandOf(float depth); // depth(0远~1近) -> 0..3
    static float BandAlpha(int band);    // 各层透明度(远景淡、近景实)

private:
    void RenderDepthLayer(const RenderContext& ctx, int band);

    std::shared_ptr<RainSystem> rain_;
    std::shared_ptr<Shader> shader_;
    std::vector<RainInstance> instances_;
};
