#pragma once
// =============================================================================
// UI 控件基元：几何矩形 + 事件描述 + 被动控件基类
//
// 交互模型（推送式）：控件不再每帧主动轮询鼠标，也不再自己做包围盒判定；
// 由 UISystem 在每帧集中判定"鼠标此刻属于哪个控件"，再把事件派发给唯一被激活
// 的控件。控件只负责三件事：报告自己的命中矩形、响应事件维护视觉状态、绘制自己。
//
// 坐标系统与 Input::GetMouseX/Y 一致：屏幕左上角原点、y 向下（ImGui 前景层同系）。
// =============================================================================
#include <functional>
#include "../Math.h"

// 自注销通道：实现在 UISystem.cpp。
// 单独抽出是为了让 UIWidget.h 不反向依赖 UISystem.h（避免循环包含），
// 同时保证控件析构时一定能从系统里摘掉自己——静态析构顺序下不会留下悬垂注册。
class UIWidget;
namespace UIInternal {
    void UnregisterWidget(UIWidget* w);
}

// ===== 命中矩形（左上角 + 尺寸） =====
struct UIRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    float Right()   const { return x + w; }
    float Bottom()  const { return y + h; }
    float CenterX() const { return x + w * 0.5f; }
    float CenterY() const { return y + h * 0.5f; }

    // 由中心 + 尺寸构造（项目内控件布局普遍以中心点描述）
    static UIRect FromCenter(float cx, float cy, float w, float h) {
        UIRect r; r.x = cx - w * 0.5f; r.y = cy - h * 0.5f; r.w = w; r.h = h; return r;
    }
    // 由边界构造
    static UIRect FromBounds(float left, float top, float right, float bottom) {
        UIRect r; r.x = left; r.y = top; r.w = right - left; r.h = bottom - top; return r;
    }
};

// ===== 事件类型 =====
enum class UIEventType {
    PointerEnter,  // 光标进入（唯一命中本控件）
    PointerLeave,  // 光标离开
    PointerDown,   // 在控件内按下左键（同时开始捕获，若控件声明 captureOnPress）
    PointerUp,     // 左键抬起——始终回送给"按下的那个控件"，即使光标已移出
    Click,         // 抬起时光标仍在本控件内：等效"确认点击"
    Drag,          // 按下期间每帧一次（含按下当帧），供滑条/滚动条跟随光标
    Wheel,         // 滚轮增量，派发给当前悬停控件
    Cancel         // 交互被强制中断（所属组关闭 / 被模态遮挡 / 控件销毁）
};

// ===== 事件载荷（只读） =====
struct UIEvent {
    UIEventType type = UIEventType::PointerEnter;
    Vec2 pointer;        // 当前光标
    Vec2 downPointer;    // 本次按下的起始位置（按下期间有效）
    float wheel = 0.0f;  // 滚轮增量（仅 Wheel 有意义）
    int button = 0;      // GLFW_MOUSE_BUTTON_LEFT
    bool pointerInside = false;  // UISystem 计算：光标是否仍在本控件命中矩形内
};

// ===== 被动控件基类 =====
// 既可直接实例化（区域类控件：悬停区 / 滚轮区 / 滚动条 / 图标热区），
// 也可由需要自绘的控件继承（ButtonSprite 覆写 GetHitRect 与 OnEvent）。
class UIWidget {
public:
    using Handler = std::function<void(const UIEvent&)>;

    UIWidget() = default;
    virtual ~UIWidget() { UIInternal::UnregisterWidget(this); }

    UIWidget(const UIWidget&) = delete;
    UIWidget& operator=(const UIWidget&) = delete;

    // ===== 命中几何（UISystem 每帧询问，而非控件自查） =====
    virtual UIRect GetHitRect() const { return rect; }
    void SetRect(const UIRect& r) { rect = r; }
    const UIRect& GetRect() const { return rect; }

    // ===== 交互门控（业务态：置灰 / 挂起 / 当前不可点） =====
    void SetEnabled(bool v) { enabled = v; }
    bool IsEnabled() const { return enabled; }

    // 按下后是否持有指针捕获（拖拽类控件置 true：抬起/拖动始终回送本控件）
    void SetCaptureOnPress(bool v) { captureOnPress = v; }
    bool WantsCaptureOnPress() const { return captureOnPress; }

    // 集中音效开关：区域类控件置 false，避免重叠区域重复发声
    void SetPlayFeedbackSFX(bool v) { playFeedbackSFX = v; }
    bool ShouldPlayFeedbackSFX() const { return playFeedbackSFX; }

    // ===== 交互状态（由事件驱动更新，供绘制侧直接读取） =====
    bool IsHovered()  const { return hovered; }
    bool IsPressed()  const { return pressed; }
    bool IsCaptured() const { return captured; }
    // 捕获标志由 UISystem 直接维护（属系统侧的指针归属，而非控件业务）
    void SetCapturedFlag(bool v) { captured = v; }

    // ===== 回调钩子 =====
    void SetOnClick(std::function<void()> cb) { onClick = std::move(cb); }
    void SetOnPress(Handler h) { onPress = std::move(h); }
    void SetOnHover(Handler enter, Handler exit) {
        onEnter = std::move(enter);
        onExit  = std::move(exit);
    }
    void SetOnDrag(Handler h)  { onDrag  = std::move(h); }
    void SetOnWheel(Handler h) { onWheel = std::move(h); }

    // ===== 事件入口（仅 UISystem 调用） =====
    // 基类实现即"标准按钮状态机"：维护 hovered/pressed 并触发对应钩子。
    // 子类如需追加行为，覆写后调用 UIWidget::OnEvent(e) 复用状态维护。
    virtual void OnEvent(const UIEvent& e) {
        switch (e.type) {
        case UIEventType::PointerEnter:
            hovered = true;
            if (onEnter) onEnter(e);
            break;
        case UIEventType::PointerLeave:
            hovered = false;
            pressed = false;
            if (onExit) onExit(e);
            break;
        case UIEventType::PointerDown:
            pressed = true;
            if (onPress) onPress(e);
            break;
        case UIEventType::Drag:
            if (onDrag) onDrag(e);
            break;
        case UIEventType::PointerUp:
            pressed = false;
            break;
        case UIEventType::Click:
            pressed = false;
            if (onClick) onClick();
            break;
        case UIEventType::Wheel:
            if (onWheel) onWheel(e);
            break;
        case UIEventType::Cancel:
            hovered = false;
            pressed = false;
            captured = false;
            if (onExit) onExit(e);
            break;
        }
    }

protected:
    UIRect rect;

    bool enabled = true;
    bool captureOnPress = false;
    bool playFeedbackSFX = true;

    bool hovered = false;
    bool pressed = false;
    bool captured = false;

    std::function<void()> onClick;
    Handler onPress;
    Handler onEnter;
    Handler onExit;
    Handler onDrag;
    Handler onWheel;
};
