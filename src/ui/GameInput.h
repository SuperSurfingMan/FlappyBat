#pragma once
// =============================================================================
// 游戏侧输入快照（GameInputFrame）与接收接口（IGameInputSink）
//
// 归属裁决之后的输入才交给玩法：UISystem 每帧读取一次设备状态，先完成 UI 命中判定
// 与事件派发（控件被按下/拖拽时该输入已由 UI 吃掉），再把"剩下的输入"打包成
// GameInputFrame 推送给注册的玩法接收者。
//
// 因此场景侧不再出现 Input::IsMousePressed / IsKeyDown 这类逐帧询问，
// 只在 OnGameInput 收到本帧快照后读取自己的操控项。
//
// 按键状态只覆盖场景显式登记的按键（UISystem::WatchKeys）：UI 系统不认识玩法键位，
// 键位到动作的语义解释仍留在场景里，推送的是状态而非结论。
// =============================================================================
#include <vector>
#include "core/Math.h"

class GameInputFrame {
public:
    struct KeyState {
        int key;
        bool pressed;   // 本帧按下边沿
        bool held;      // 持续按住
        bool released;  // 本帧松开边沿
    };

    // ===== 只读查询（未登记的按键一律返回 false） =====
    bool Pressed(int key) const;
    bool Held(int key) const;
    bool Released(int key) const;

    // 指针：已被 UI 归属裁决过滤——按下/拖动落在控件上时这里静默，不会误触发玩法
    bool PointerPressed(int button) const;
    bool PointerHeld(int button) const;
    bool PointerReleased(int button) const;

    Vec2 GetPointer() const { return pointer; }
    float GetWheel() const { return wheel; }

    // ===== 由 UISystem 每帧填充（场景侧不应调用） =====
    void SetPointer(Vec2 p, float w) { pointer = p; wheel = w; }
    void SetKeys(std::vector<KeyState> ks) { keys = std::move(ks); }
    void SetMouseButton(int button, bool pressed, bool held, bool released);

private:
    struct MouseButtonState {
        bool pressed = false;
        bool held = false;
        bool released = false;
    };
    static constexpr int kButtonCount = 3;   // 与 Input::MouseButtonCount 一致

    std::vector<KeyState> keys;
    MouseButtonState buttons[kButtonCount] = {};
    Vec2 pointer;
    float wheel = 0.0f;
};

// 玩法输入接收者（当前实现：MainScene）
class IGameInputSink {
public:
    virtual ~IGameInputSink() = default;
    // 每帧一次，由 UISystem::Dispatch 在归属裁决与控件派发之后调用，先于 Scene::Update
    virtual void OnGameInput(const GameInputFrame& frame) = 0;
};
