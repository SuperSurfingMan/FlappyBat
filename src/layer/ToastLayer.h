#pragma once
#include "Layer.h"
#include "../Math.h"
#include <string>
#include <memory>

class Texture2D;

class ToastLayer : public Layer {
public:
    ToastLayer() = default;
    ~ToastLayer() = default;

    ToastLayer(float duration, std::string text, Vec3 color, float textSize, Vec2 position);

    void Update(float dt);
    void Render(SpriteRenderer& renderer);
    void Reset();

    void SetColor(Vec3 _color);
    void SetText(std::string _text);

    // 可选背景纹理：设置后在文字下方以 position 为中心绘制（如成就提示面板）
    void SetBackground(std::shared_ptr<Texture2D> tex);

    bool IsRunning() const;

    void Activate();
private:
    float duration;
    std::string text;
    Vec3 color;
    float textSize;
    Vec2 position;

    float timer = 0.f; 
    float alpha = 0.f;

    bool running = false;

    std::shared_ptr<Texture2D> bgTex; // 可选背景（默认空 = 纯文字，保持原行为）

private:
    void DrawImGUILayout();
    void DrawToastText(float alphaVal);

};
