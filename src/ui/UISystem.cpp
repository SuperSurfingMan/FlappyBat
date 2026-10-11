#include "UISystem.h"

#include "../Input.h"
#include "../audio/AudioSystem.h"

#include <imgui.h>
#include <algorithm>

// =============================================================================
// UISystem：每帧一次命中判定 + 事件派发（详见 UISystem.h 的职责边界说明）
// =============================================================================

namespace UIInternal {
    // 控件自注销：UIWidget 析构时调用（实现在此以便 UIWidget.h 不依赖 UISystem.h）
    void UnregisterWidget(UIWidget* w) {
        UISystem::Get().Remove(w);
    }
}

UISystem& UISystem::Get() {
    // 泄漏式单例：场景/图层持有的控件析构时会回调 Remove，
    // 若本实例在静态析构阶段先被销毁，自注销将访问已失效对象（项目历史段错误根因）。
    static UISystem* instance = new UISystem();
    return *instance;
}

// ===== 组管理 =====

UISystem::UIGroupId UISystem::CreateGroup(int layer) {
    Group g;
    g.layer = layer;
    g.order = nextGroupOrder++;
    g.enabled = false;  // 默认关闭：由所属场景 OnEnter / 面板打开时启用
    groups.push_back(g);

    // 组间层叠顺序与组数量同生命周期：仅在创建时重排（组不销毁）
    sortedGroups.push_back((UIGroupId)groups.size() - 1);
    std::stable_sort(sortedGroups.begin(), sortedGroups.end(),
        [this](int a, int b) {
            if (groups[a].layer != groups[b].layer) return groups[a].layer > groups[b].layer;
            return groups[a].order > groups[b].order;
        });

    return (UIGroupId)groups.size() - 1;
}

void UISystem::SetGroupEnabled(UIGroupId id, bool enabled) {
    Group* g = FindGroup(id);
    if (!g || g->enabled == enabled) return;
    g->enabled = enabled;
    // 关闭组的悬停/按下残留在下帧 Dispatch 开头由 CancelStaleTargets 统一清理
}

bool UISystem::IsGroupEnabled(UIGroupId id) const {
    const Group* g = FindGroup(id);
    return g && g->enabled;
}

void UISystem::PushModal(UIGroupId id) {
    if (FindGroup(id) == nullptr) return;
    // 幂等：已在栈上则仅置顶
    modalStack.erase(std::remove(modalStack.begin(), modalStack.end(), id), modalStack.end());
    modalStack.push_back(id);
}

void UISystem::PopModal(UIGroupId id) {
    modalStack.erase(std::remove(modalStack.begin(), modalStack.end(), id), modalStack.end());
}

// ===== 控件注册 =====

void UISystem::Add(UIGroupId groupId, UIWidget* widget) {
    Group* g = FindGroup(groupId);
    if (!g || !widget) return;
    if (std::find(g->widgets.begin(), g->widgets.end(), widget) != g->widgets.end()) return;
    g->widgets.push_back(widget);
}

void UISystem::Remove(UIWidget* widget) {
    if (!widget) return;
    for (Group& g : groups) {
        g.widgets.erase(std::remove(g.widgets.begin(), g.widgets.end(), widget), g.widgets.end());
    }
    // 控件正在销毁：只摘引用，不发事件
    if (hovered == widget) hovered = nullptr;
    if (pressed == widget) {
        pressed = nullptr;
        heldMask &= ~(1u << GLFW_MOUSE_BUTTON_LEFT);
    }
    if (captured == widget) captured = nullptr;
}

void UISystem::ClearWidgetState(UIGroupId id) {
    Group* g = FindGroup(id);
    if (!g) return;
    for (UIWidget* w : g->widgets) {
        if (hovered == w) hovered = nullptr;
        if (pressed == w) {
            pressed = nullptr;
            heldMask &= ~(1u << GLFW_MOUSE_BUTTON_LEFT);
        }
        if (captured == w) captured = nullptr;
        w->SetCapturedFlag(false);
        Deliver(w, UIEventType::Cancel);
    }
}

// ===== 每帧驱动 =====

void UISystem::BeginFrame(float /*dt*/) {
    // 整帧的鼠标快照：Dispatch 之外不再有任何地方读取鼠标
    pointer = Vec2(Input::GetMouseX(), Input::GetMouseY());
    wheel = (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetIO().MouseWheel : 0.0f;
}

void UISystem::Dispatch() {
    const int kBtn = GLFW_MOUSE_BUTTON_LEFT;
    const bool pressEdge   = Input::IsMousePressed(kBtn);
    const bool down        = Input::IsMouseDown(kBtn);
    const bool releaseEdge = Input::IsMouseReleased(kBtn);

    // 0) 上帧目标本帧已失活（组关闭 / 被模态遮挡 / 注销）：先清理，避免残留高亮
    CancelStaleTargets();

    // 1) 整帧唯一一次命中判定：谁该被激活，只在这里决定
    UIWidget* hit = PickHit(pointer.x, pointer.y);

    // 2) 悬停差分
    if (hit != hovered) {
        if (hovered) Deliver(hovered, UIEventType::PointerLeave);
        hovered = hit;
        if (hovered) {
            if (hovered->ShouldPlayFeedbackSFX())
                AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Hover), 0.45f);
            Deliver(hovered, UIEventType::PointerEnter);
        }
    }

    // 3) 按下边沿：命中者取得按下归属，此后 Up/Drag 一律回送给它
    if (pressEdge && hovered) {
        pressed = hovered;
        downPointer = pointer;
        if (pressed->WantsCaptureOnPress()) {
            captured = pressed;
            captured->SetCapturedFlag(true);
        }
        heldMask |= (1u << kBtn);  // 本键由 UI 消费：游戏侧不得再用它触发玩法输入
        if (pressed->ShouldPlayFeedbackSFX())
            AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Press), 0.55f);
        Deliver(pressed, UIEventType::PointerDown);
        Deliver(pressed, UIEventType::Drag);  // 按下当帧同走一次：点轨道/点滑条即定位
    } else if (pressed && down) {
        Deliver(pressed, UIEventType::Drag);
    }

    // 4) 滚轮：发给当前悬停控件（列表热区），不再由场景自行轮询 io.MouseWheel
    if (wheel != 0.0f && hovered) Deliver(hovered, UIEventType::Wheel);

    // 5) 抬起：始终回送给按下的控件；仅当光标仍在其中才判定为 Click
    if (pressed && (!down || releaseEdge)) {
        UIWidget* target = pressed;
        pressed = nullptr;
        if (captured == target) {
            captured = nullptr;
            target->SetCapturedFlag(false);
        }
        const bool inside = target->IsEnabled() &&
                            target->GetHitRect().Contains(pointer.x, pointer.y);
        Deliver(target, UIEventType::PointerUp);
        if (inside) {
            if (target->ShouldPlayFeedbackSFX())
                AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Click), 1.0f);
            Deliver(target, UIEventType::Click);
        }
    }
    if (!down) heldMask &= ~(1u << kBtn);

    // 6) 玩法输入推送：归属裁决之后才下发，UI 已拿走的输入不会到达场景
    PushGameInput(pressEdge, down, releaseEdge);
}

void UISystem::EndFrame() {
    // 一帧性输入缓存在此出清：滚轮增量只对当帧的派发有效
    wheel = 0.0f;
}

// ===== 内部实现 =====

UISystem::Group* UISystem::FindGroup(UIGroupId id) {
    if (id < 0 || id >= (int)groups.size()) return nullptr;
    return &groups[id];
}

const UISystem::Group* UISystem::FindGroup(UIGroupId id) const {
    if (id < 0 || id >= (int)groups.size()) return nullptr;
    return &groups[id];
}

UISystem::UIGroupId UISystem::GroupOf(const UIWidget* w) const {
    for (size_t i = 0; i < groups.size(); ++i) {
        const std::vector<UIWidget*>& vs = groups[i].widgets;
        if (std::find(vs.begin(), vs.end(), const_cast<UIWidget*>(w)) != vs.end())
            return (UIGroupId)i;
    }
    return kInvalidGroup;
}

bool UISystem::IsWidgetLive(const UIWidget* w) const {
    const UIGroupId gid = GroupOf(w);
    if (gid == kInvalidGroup) return false;
    if (!groups[gid].enabled) return false;
    // 模态期间仅栈顶组的控件可参与命中，其余组整体让位
    if (!modalStack.empty() && modalStack.back() != gid) return false;
    return true;
}

void UISystem::CancelStaleTargets() {
    UIWidget* targets[3] = { hovered, pressed, captured };
    for (UIWidget* w : targets) {
        if (!w || IsWidgetLive(w)) continue;
        if (hovered == w) hovered = nullptr;
        if (pressed == w) {
            pressed = nullptr;
            heldMask &= ~(1u << GLFW_MOUSE_BUTTON_LEFT);
        }
        if (captured == w) captured = nullptr;
        w->SetCapturedFlag(false);
        Deliver(w, UIEventType::Cancel);
    }
}

UIWidget* UISystem::PickHit(float px, float py) const {
    // 模态优先：栈顶组有效时，命中解析只在该组内进行（下层组完全让位）
    if (!modalStack.empty()) {
        const Group* top = FindGroup(modalStack.back());
        if (top && top->enabled) {
            for (auto it = top->widgets.rbegin(); it != top->widgets.rend(); ++it) {
                UIWidget* w = *it;
                if (w->IsEnabled() && w->GetHitRect().Contains(px, py)) return w;
            }
            return nullptr;
        }
    }

    // 常规：组按 (layer, 创建序) 降序，组内按注册序降序（后注册者在上）
    for (UIGroupId gid : sortedGroups) {
        const Group& g = groups[gid];
        if (!g.enabled) continue;
        for (auto it = g.widgets.rbegin(); it != g.widgets.rend(); ++it) {
            UIWidget* w = *it;
            if (w->IsEnabled() && w->GetHitRect().Contains(px, py)) return w;
        }
    }
    return nullptr;
}

void UISystem::Deliver(UIWidget* w, UIEventType type) {
    if (!w) return;
    UIEvent e;
    e.type = type;
    e.pointer = pointer;
    e.downPointer = downPointer;
    e.button = GLFW_MOUSE_BUTTON_LEFT;
    e.wheel = wheel;
    e.pointerInside = w->GetHitRect().Contains(pointer.x, pointer.y);
    w->OnEvent(e);
}

// ===== 玩法输入推送 =====

void UISystem::WatchKeys(std::vector<int> keys) {
    // 合并去重：多次/多处声明都不会重复采集
    for (int k : keys) {
        if (std::find(watchedKeys.begin(), watchedKeys.end(), k) == watchedKeys.end())
            watchedKeys.push_back(k);
    }
}

void UISystem::AddGameInputSink(IGameInputSink* sink) {
    if (!sink) return;
    if (std::find(gameSinks.begin(), gameSinks.end(), sink) == gameSinks.end())
        gameSinks.push_back(sink);
}

void UISystem::RemoveGameInputSink(IGameInputSink* sink) {
    gameSinks.erase(std::remove(gameSinks.begin(), gameSinks.end(), sink), gameSinks.end());
}

void UISystem::PushGameInput(bool leftPressEdge, bool leftDown, bool leftReleaseEdge) {
    if (gameSinks.empty()) return;

    GameInputFrame f;
    f.SetPointer(pointer, wheel);

    std::vector<GameInputFrame::KeyState> ks;
    ks.reserve(watchedKeys.size());
    for (int key : watchedKeys) {
        ks.push_back(GameInputFrame::KeyState{
            key,
            Input::IsKeyPressed(key),
            Input::IsKeyDown(key),
            Input::IsKeyReleased(key)});
    }
    f.SetKeys(std::move(ks));

    // 左键：本帧由控件持有（按下归属或按住未松）则不下发给玩法
    const bool uiOwnsLeft = (heldMask & (1u << GLFW_MOUSE_BUTTON_LEFT)) != 0u || pressed != nullptr;
    f.SetMouseButton(GLFW_MOUSE_BUTTON_LEFT,
                     leftPressEdge && !uiOwnsLeft,
                     leftDown && !uiOwnsLeft,
                     leftReleaseEdge && !uiOwnsLeft);

    // 其余按键不参与 UI 竞争，仅保留"光标悬停在控件上时不触发玩法"的既有语义
    const bool overUI = (hovered != nullptr);
    f.SetMouseButton(GLFW_MOUSE_BUTTON_RIGHT,
                     Input::IsMousePressed(GLFW_MOUSE_BUTTON_RIGHT) && !overUI,
                     Input::IsMouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !overUI,
                     Input::IsMouseReleased(GLFW_MOUSE_BUTTON_RIGHT) && !overUI);
    f.SetMouseButton(GLFW_MOUSE_BUTTON_MIDDLE,
                     Input::IsMousePressed(GLFW_MOUSE_BUTTON_MIDDLE) && !overUI,
                     Input::IsMouseDown(GLFW_MOUSE_BUTTON_MIDDLE) && !overUI,
                     Input::IsMouseReleased(GLFW_MOUSE_BUTTON_MIDDLE) && !overUI);

    // 多播：玩法与各 UI 快捷键层依次拿到同一份已裁决快照
    for (IGameInputSink* sink : gameSinks) {
        if (sink) sink->OnGameInput(f);
    }
}

// ===== GameInputFrame =====

bool GameInputFrame::Pressed(int key) const {
    for (const KeyState& k : keys) if (k.key == key) return k.pressed;
    return false;
}

bool GameInputFrame::Held(int key) const {
    for (const KeyState& k : keys) if (k.key == key) return k.held;
    return false;
}

bool GameInputFrame::Released(int key) const {
    for (const KeyState& k : keys) if (k.key == key) return k.released;
    return false;
}

void GameInputFrame::SetMouseButton(int button, bool pressed, bool held, bool released) {
    if (button < 0 || button >= kButtonCount) return;
    buttons[button].pressed = pressed;
    buttons[button].held = held;
    buttons[button].released = released;
}

bool GameInputFrame::PointerPressed(int button) const {
    if (button < 0 || button >= kButtonCount) return false;
    return buttons[button].pressed;
}

bool GameInputFrame::PointerHeld(int button) const {
    if (button < 0 || button >= kButtonCount) return false;
    return buttons[button].held;
}

bool GameInputFrame::PointerReleased(int button) const {
    if (button < 0 || button >= kButtonCount) return false;
    return buttons[button].released;
}
