#pragma once
#include "layer/Layer.h"
#include "core/Math.h"
#include <string>
#include <memory>

class Texture2D;

class ToastLayer : public Layer {
public:
    ToastLayer();
    ~ToastLayer() = default;

    ToastLayer(float duration, std::string text, Vec3 color, float textSize, Vec2 position);

    void Reset() override;

    void SetColor(Vec3 _color);
    void SetText(std::string _text);

    // 可选背景纹理：设置后在文字下方以 position 为中心绘制（如成就提示面板）
    void SetBackground(std::shared_ptr<Texture2D> tex);

    bool IsRunning() const;

    void Activate();

    // 成就提示需要压在场景 HUD 之上；Toast 在 HUD 之前绘制，保持原有层级
    LayerRenderPhase RenderPhase() const override { return LayerRenderPhase::BeforeHud; }

protected:
    void OnUpdate(float dt) override;
    void OnDraw(SpriteRenderer& renderer) override;

private:
    float duration = 0.0f;
    std::string text;
    Vec3 color;
    float textSize = 0.0f;
    Vec2 position;

    float timer = 0.f;
    float alpha = 0.f;

    bool running = false;

    std::shared_ptr<Texture2D> bgTex; // 可选背景（默认空 = 纯文字，保持原行为）

private:
    void DrawImGUILayout();
    void DrawToastText(float alphaVal);
};
