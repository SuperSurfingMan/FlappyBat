#pragma once

class SpriteRenderer;

// 渲染器统一抽象：负责把某个子系统的数据绘制到屏幕。
// 具体渲染器通过 Init/构造持有数据源，Render 时读取数据并提交绘制，
// 自行管理自己的 shader、VAO/VBO 与渲染状态（通过 BlendScope）。
class IRenderer
{
public:
    virtual ~IRenderer() = default;
    virtual void Render(SpriteRenderer& spriteRenderer) = 0;
};
