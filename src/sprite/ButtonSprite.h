#pragma once
#include "Sprite.h"
#include <functional>
#include <string>
#include <vector>

#include "../ui/UIBuilder.h"
#include "../ui/UIWidget.h"

// ===== 按钮控件（被动式） =====
// 交互不再由本控件轮询鼠标：命中区域判定、悬停/按下/点击的归属全部交给 UISystem，
// 每帧由 UISystem 统一判定后把 UIEvent 推送过来（见 src/ui/UISystem.h）。
// 本控件只做两件事：报告命中矩形（按布局几何，不随动效漂移）+ 按状态自绘。
class ButtonSprite : public Sprite, public UIWidget {
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

    // 仅推进 hover/press 视觉补间；鼠标判定与事件派发都在 UISystem 中
    void Update(float dt) override;

    // 程序化绘制：调用方需要在 SpriteRenderer 之后单独调用 DrawLayout()
    void DrawLayout();

    void Reset() override;

    // 命中矩形取布局几何（base 位置/尺寸）：hover 放大与按下位移不再改变可点区域
    UIRect GetHitRect() const override {
        return UIRect::FromCenter(basePosition.x, basePosition.y, baseScale.x, baseScale.y);
    }

    // 显示与交互同步开关：旧代码用 setActive(false) 同时抑制绘制与输入，
    // 拆分后由本入口一次设置（绘制仍属 Sprite，交互属 UIWidget）
    void SetShown(bool v) { setActive(v); SetEnabled(v); }

    // 抑制默认纹理渲染（程序化绘制）
    virtual std::vector<std::shared_ptr<Sprite>> GetSubspriteList() const override { return {}; }

private:
    Style style = Style::Primary;
    UIBuilder::IconKind iconKind = UIBuilder::IconKind::Pause;
    bool isIconButton = false;

    std::string text;
    float textSize = UITheme::FontButton;

    Vec2 basePosition;
    Vec2 baseScale;

    const float hoverScaleFactor = UITheme::HoverScale;
    const float pressedScaleFactor = UITheme::PressedScale;
    const float pressedOffsetY = UITheme::PressedOffsetY;

    void InternalUpdateAnim(float dt);
};
