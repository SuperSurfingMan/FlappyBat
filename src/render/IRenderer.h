#pragma once

#include "render/WorldSlot.h"
#include "render/RenderContext.h"

#include <vector>

// =============================================================================
// 渲染器统一抽象(方案一):负责把某个子系统的数据绘制到屏幕。
// - 具体渲染器通过 Init 持有数据源,自行管理 shader、VAO/VBO 与渲染状态
//   (经 BlendScope);普通 Sprite 一律经 ctx.sprites 提交;
// - Update 推进自驱动粒子(数据来自外部模拟的渲染器用默认空实现);
// - Render 只响应 Slots() 中声明认领的 WorldSlot,由 WeatherDirector 的
//   槽位分发表多态调度——接口不再只是名义契约,而是真正的分发路径。
// =============================================================================
class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // 每帧推进自驱动粒子(如风线);依赖外部模拟数据的渲染器保持空实现
    virtual void Update(float dt) { (void)dt; }

    // 绘制本渲染器认领槽位中与 slot 匹配的内容;非认领槽位调用为空操作
    virtual void Render(const RenderContext& ctx, WorldSlot slot) = 0;

    // 本渲染器认领的槽位声明(渲染顺序文档的一部分):WeatherDirector 在
    // Init 时据此建立 槽位->渲染器 分发表,渲染期零扫描
    virtual std::vector<WorldSlot> Slots() const = 0;
};
