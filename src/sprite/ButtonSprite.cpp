#include "ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../Math.h"
#include "../Util.h"
#include <iostream>

// ====================== 文本按钮 ======================
ButtonSprite::ButtonSprite(Style style, Vec2 buttonPosition, Vec2 buttonScale, const std::string& text)
    : style(style), text(text), isIconButton(false)
{
    SetPosition(buttonPosition.x, buttonPosition.y);
    SetScale(buttonScale.x, buttonScale.y);
    basePosition = buttonPosition;
    baseScale = buttonScale;
    // 关键：禁用纹理渲染（程序化绘制）
    texture = nullptr;
}

// ====================== 图标按钮 ======================
ButtonSprite::ButtonSprite(UIBuilder::IconKind icon, Vec2 buttonPosition, float size)
    : style(Style::Icon), iconKind(icon), isIconButton(true)
{
    SetPosition(buttonPosition.x, buttonPosition.y);
    SetScale(size, size);
    basePosition = buttonPosition;
    baseScale = {size, size};
    texture = nullptr;
}

void ButtonSprite::Update(float dt) {
    if (!isActive) return;
    InternalUpdateAnim(dt);
    HandleInput();
}

void ButtonSprite::InternalUpdateAnim(float dt) {
    // 状态机驱动的视觉过渡：scale（文字颜色由 DrawLayout 按状态即时计算）
    float targetScale = isPressed ? pressedScaleFactor : (isCursorOver ? hoverScaleFactor : 1.0f);
    float currentScale = (scale.x / baseScale.x);
    float nextScale = currentScale + (targetScale - currentScale) * std::min(1.0f, dt * 12.0f);
    Vec2 s = baseScale;
    s.x *= nextScale;
    s.y *= nextScale;
    scale = s;

    // 垂直位置：按下下移（y 向下为正）
    float targetY = isPressed ? basePosition.y + pressedOffsetY : basePosition.y;
    position.y += (targetY - position.y) * std::min(1.0f, dt * 15.0f);
}

void ButtonSprite::HandleInput() {
    // 光标（GLFW 回调）与按钮 position 同为左上角原点、y 向下的屏幕坐标，直接比较
    Point2D cursorPos = Input::GetCursorPosition();

    bool isOver = cursorPos.x > position.x - scale.x * 0.5f &&
                  cursorPos.x < position.x + scale.x * 0.5f &&
                  cursorPos.y > position.y - scale.y * 0.5f &&
                  cursorPos.y < position.y + scale.y * 0.5f;

    if (isOver) {
        if (!isCursorOver) {
            // 鼠标进入：钝感轻提示音（比按下音效更低频柔和）
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Hover), 0.45f);
        }
        isCursorOver = true;
        if (Input::IsMousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
            isPressed = true;
            // 按下瞬间：轻量触觉反馈音
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Press), 0.55f);
        }
        if (Input::IsMouseReleased(GLFW_MOUSE_BUTTON_LEFT) && isPressed) {
            isPressed = false;
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Click), 1.0f);
            if (onClick) onClick();
        }
    } else {
        if (isCursorOver) {
            // 鼠标离开
        }
        isCursorOver = false;
        isPressed = false;
    }
}

void ButtonSprite::DrawLayout() {
    if (!isActive) return;

    float cx = position.x;
    float cy = position.y;
    float w = scale.x;
    float h = scale.y;

    bool isPrimary = (style == Style::Primary);
    bool isDanger = (style == Style::Danger);
    bool isHover = isCursorOver;
    bool isPressedHere = isPressed;

    if (isIconButton) {
        // 图标按钮：绘制背景板 + 对应图标
        switch (iconKind) {
            case UIBuilder::IconKind::Pause:   UIBuilder::DrawIconPause(cx, cy, w, isHover, isPressedHere); break;
            case UIBuilder::IconKind::Play:    UIBuilder::DrawIconPlay(cx, cy, w, isHover, isPressedHere); break;
            case UIBuilder::IconKind::Home:    UIBuilder::DrawIconHome(cx, cy, w, isHover, isPressedHere); break;
            case UIBuilder::IconKind::Quit:    UIBuilder::DrawIconQuit(cx, cy, w, isHover, isPressedHere); break;
            case UIBuilder::IconKind::Restart: UIBuilder::DrawIconRestart(cx, cy, w, isHover, isPressedHere); break;
            case UIBuilder::IconKind::Rank:    UIBuilder::DrawIconRank(cx, cy, w, isHover, isPressedHere); break;
        }
    } else {
        // 文本按钮：底板 + 文字
        UIBuilder::DrawButton(cx, cy, w, h, isPrimary, isHover, isPressedHere, isDanger);
        UIBuilder::DrawButtonText(text, cx, cy, textSize, isHover, isPressedHere, isDanger);
    }
}

void ButtonSprite::Reset() {
    Sprite::Reset();
    isCursorOver = false;
    isPressed = false;
    SetPosition(basePosition.x, basePosition.y);
    SetScale(baseScale.x, baseScale.y);
}
