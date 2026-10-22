#pragma once

#include "../Texture2D.h"
#include "../Animation.h"
#include "../Math.h"

class Sprite {
public:
    Sprite();
    virtual ~Sprite();

    explicit Sprite(std::shared_ptr<Texture2D> texture);

    void SetTexture(std::shared_ptr<Texture2D> texture);
    void SetPosition(float x, float y);
    void SetScale(float x, float y);

    const Vec2& GetPosition() const { return position; }
    const Vec2& GetScale() const { return scale; }

    // 计算 4x4 变换矩阵（column-major，OpenGL 标准）
    void GetTransform(float out[16]) const;

    virtual void Update(float dt);

    void InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, size_t frameCount);
    void InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, size_t rows, size_t columns);
    void InitAnimationByTexture(std::shared_ptr<Texture2D> spriteSheet, std::vector<float> uvSegPoints);
    void InitEmptyAnimation();

    virtual void SwitchToAnimation(int animIndex); // 可以根据需要添加更多动画控制方法

    void setAnimationCallback(std::function<void()> callback);

    void setActive(bool active) { isActive = active; }
    bool getActive() const { return isActive; }

    const std::shared_ptr<Texture2D>& GetTexture() const;
    const std::unique_ptr<Animation>& GetAnimation() const;

    virtual void Reset();
public:
    void SetAlpha(float a);
    void SetColor(float r, float g, float b, float a);
    void SetRotation(Vec2 r);

    const float* GetColor() const;

    FrameUV GetFrameUV() const;

public:
    void SetGlow(float g);
    float GetGlow() const;

    // 亮度抠像：用于把黑底发光素材渲染为透明叠加
    void SetLumaKey(bool enabled, float thresh = 0.08f, float soft = 0.12f);
    bool HasLumaKey() const { return useLumaKey; }
    float GetLumaThresh() const { return lumaThresh; }
    float GetLumaSoft() const { return lumaSoft; }

protected:
    // 字段
    Vec2 position;   // NDC 坐标
    Vec2 rotation = {1, 0};
    Vec2 scale{1.0f, 1.0f};
    std::shared_ptr<Texture2D> texture;
    FrameUV currentFrameUV = {{0.f, 0.f}, {1.f, 1.f}};

    std::unique_ptr<Animation> animation; // 使用智能指针管理 Animation 生命周期

    bool isActive = true; // 可选：用于控制精灵是否参与更新和渲染

    float color[4] = {1.f, 1.f, 1.f, 1.f}; // RGBA
    float glow = 0.0f; // 发光

    bool useLumaKey = false;
    float lumaThresh = 0.08f;
    float lumaSoft = 0.12f;
};
