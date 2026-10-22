#include "SpriteRenderer.h"
#include "ShaderLibrary.h"
#include "sprite/ColorfulSprite.h"

#include <glad/glad.h>
#include <iostream>
// 单位 quad（-0.5 ~ 0.5）
static float quadVertices[] = {
    // pos        // uv
    -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  1.0f, 1.0f,

    -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.0f, 1.0f
};

SpriteRenderer::SpriteRenderer()
{
    shader = ShaderLibrary::Instance().Load(
        "sprite",
        "assets/shaders/sprite.vert",
        "assets/shaders/sprite.frag"
    );

    waveShader = ShaderLibrary::Instance().Load(
        "sprite_wave",
        "assets/shaders/sprite_wave.vert",
        "assets/shaders/sprite_wave.frag"
    );

    InitRenderData();
}

SpriteRenderer::~SpriteRenderer() = default;

void SpriteRenderer::InitRenderData()
{
    VAO.Bind();
    VBO.BufferData(quadVertices, sizeof(quadVertices), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        (void*)0
    );

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        (void*)(2 * sizeof(float))
    );

    VAO.Unbind();
}

void SpriteRenderer::Begin(const float* projection)
{
    Begin(shader.get(), projection);
}

void SpriteRenderer::Begin(Shader* usedShader, const float* projection)
{
    usedShader->Use();
    usedShader->SetMat4("uProjection", projection);
}

void SpriteRenderer::SetProjection(const float* spriteProjection) {
    // 拷贝 16 个 float：调用方可安全传入栈上临时数组（见头文件说明）
    for (int i = 0; i < 16; ++i) {
        this->spriteProjection[i] = spriteProjection[i];
    }
}

void SpriteRenderer::Draw_UV(const std::vector<std::shared_ptr<Sprite>>& sprites)
{
    for (const auto& sprite : sprites) {
        Draw_UV(sprite);
    }
}

void SpriteRenderer::Draw_UV(const std::shared_ptr<Sprite>& sprite)
{
    // 无纹理（如 ButtonSprite，完全程序化绘制）或已停用则不参与 GL 绘制；
    // 组合渲染由调用方用批量重载（vector）控制顺序，不再向精灵索取子节点
    if (!sprite->GetTexture() || !sprite->getActive()) return;

    Begin(spriteProjection);

    float model[16];
    sprite->GetTransform(model);

    shader->SetMat4("uModel", model);
    shader->SetInt("uTexture", 0);

    const float* color = sprite->GetColor();
    shader->SetVec4("uColor", color[0], color[1], color[2], color[3]);
    shader->SetFloat("uGlow", sprite->GetGlow());

    // 亮度抠像
    if (sprite->HasLumaKey()) {
        shader->SetFloat("uLumaKey", 1.0f);
        shader->SetFloat("uLumaThresh", sprite->GetLumaThresh());
        shader->SetFloat("uLumaSoft", sprite->GetLumaSoft());
    } else {
        shader->SetFloat("uLumaKey", 0.0f);
        shader->SetFloat("uLumaThresh", 0.0f);
        shader->SetFloat("uLumaSoft", 0.0f);
    }

    // 新增：FrameUV
    FrameUV uv = sprite->GetFrameUV();
    shader->SetVec4("uFrameUV",
        uv.uvMin.x,
        uv.uvMin.y,
        uv.uvMax.x,
        uv.uvMax.y);

    sprite->GetTexture()->Bind(0);

    VAO.Bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void SpriteRenderer::Draw_UV_Wave(const std::shared_ptr<Sprite>& sprite)
{
    if (!sprite->GetTexture() || !sprite->getActive()) return;

    Begin(waveShader.get(), spriteProjection);

    float model[16];
    sprite->GetTransform(model);

    waveShader->SetMat4("uModel", model);
    waveShader->SetInt("uTexture", 0);

    const float* color = sprite->GetColor();
    waveShader->SetVec4("uColor", color[0], color[1], color[2], color[3]);

    // 归一化时间：用于周期控制
    auto colorfulSprite = std::static_pointer_cast<ColorfulSprite>(sprite);
    waveShader->SetFloat("uTime01", colorfulSprite->GetProgress());

    // 效果参数，可按需要调
    waveShader->SetFloat("uWaveScale", 3.0f);
    waveShader->SetFloat("uWaveSpeed", 1.0f);
    waveShader->SetFloat("uHueStrength", 0.12f);
    waveShader->SetFloat("uMixStrength", 1.f);
    waveShader->SetFloat("uBrightness", 4.0f);
    waveShader->SetFloat("uWaveSharpness", 1.0f);

    // FrameUV：如果你在做图集裁剪，这个仍然要传
    FrameUV uv = sprite->GetFrameUV();
    waveShader->SetVec4("uFrameUV",
        uv.uvMin.x,
        uv.uvMin.y,
        uv.uvMax.x,
        uv.uvMax.y);

    sprite->GetTexture()->Bind(0);

    VAO.Bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void SpriteRenderer::End()
{
    // 预留：以后在这里 flush batch
}
