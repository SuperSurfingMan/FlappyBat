#pragma once
#include "Sprite.h"
#include <functional>
#include <string>
#include <vector>

#include "../Input.h"
#include "../audio/AudioSystem.h"
#include "../ui/UIBuilder.h"
#include <GLFW/glfw3.h>
#include "imgui.h"

class ButtonSprite : public Sprite {
public:
    enum class Style {
        Primary,    // 主按钮（亮金描边，长条）
        Secondary,  // 次级按钮（暗金描边，长条）
        Danger,     // 危险按钮（血红描边，用于清空记录等破坏性操作）
        Icon        // 仅图标（包围盒 = 正方形）
    };

    ButtonSprite() = default;
    ~ButtonSprite() = default;

    // ===== 文本按钮 =====
    ButtonSprite(Style style, Vec2 buttonPosition, Vec2 buttonScale, const std::string& text);

    // ===== 图标按钮 =====
    ButtonSprite(UIBuilder::IconKind icon, Vec2 buttonPosition, float size);

    void Update(float dt) override;

    // 程序化绘制：调用方需要在 SpriteRenderer 之后单独调用 DrawLayout()
    void DrawLayout();

    void Reset() override;

    void SetOnClick(std::function<void()>&& callback) { onClick = std::move(callback); }

    // 抑制默认纹理渲染（启用程序化绘制）
    virtual std::vector<std::shared_ptr<Sprite>> GetSubspriteList() const override { return {}; }

private:
    Style style = Style::Primary;
    UIBuilder::IconKind iconKind = UIBuilder::IconKind::Pause;
    bool isIconButton = false;

    std::function<void()> onClick;

    std::string text;
    float textSize = UITheme::FontButton;

    Vec2 basePosition;
    Vec2 baseScale;

    bool isCursorOver = false;
    bool isPressed = false;

    const float hoverScaleFactor = UITheme::HoverScale;
    const float pressedScaleFactor = UITheme::PressedScale;
    const float pressedOffsetY = UITheme::PressedOffsetY;

    void HandleInput();
    void InternalUpdateAnim(float dt);
};
