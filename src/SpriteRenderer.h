#pragma once

#include <memory>
#include "Shader.h"
#include "VertexArray.h"
#include "VertexBuffer.h"
#include "sprite/Sprite.h"
#include "Math.h"

class SpriteRenderer {
public:
    SpriteRenderer();
    ~SpriteRenderer();

    // 每帧开始
    void Begin(const float* projection);
    void Begin(Shader* usedShader, const float* projection);

    // 画所有 sprite
    void Draw_UV(const std::vector<std::shared_ptr<Sprite>>& sprites);
    void Draw_UV(const std::shared_ptr<Sprite>& sprite);

    void SetProjection(const float* spriteProjection);

    // 每帧结束（目前空，给以后 batch 用）
    void End();
private:
    VertexArray VAO;
    VertexBuffer VBO;

    std::shared_ptr<Shader> shader;

    void InitRenderData();

    // 持有投影矩阵的「副本」而非指针：调用方常传入栈上临时数组
    // （如 MainScene 抖动时的 proj[16]），存指针会在其返回后悬空，
    // 导致后续帧用被覆盖的垃圾矩阵绘制（表现为世界全黑）。
    float spriteProjection[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};
