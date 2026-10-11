#pragma once
// =============================================================================
// UISystem —— 全局 UI 事件派发中枢（推送式交互的唯一命中判定者）
//
// 职责边界：
//   1. 每帧读取一次鼠标（位置 / 左键边沿 / 滚轮），做**唯一命中判定**；
//   2. 把 Enter/Leave/Down/Up/Click/Drag/Wheel/Cancel 事件派发给真正需要激活的控件；
//   3. 维护指针捕获、按钮消费标志与集中反馈音效；
//   4. 不绘制任何东西——控件仍在各自的 Render()/DrawLayout() 中自绘，渲染顺序不变。
//
// 分组（Group）与层叠：
//   控件按组注册，组有 layer（越大越靠上）。命中解析时组间比 layer、组内比注册顺序
//   （后注册者在上），因此"上层面板的按钮"天然优先于"被它盖住的列表热区"。
//
// 模态（Modal）：
//   模态栈非空时，只有栈顶组的控件参与命中——下层组的所有控件自动失效，
//   无需再逐个 setActive(false) 挂起场景交互。
// =============================================================================
#include "UIWidget.h"
#include "GameInput.h"
#include <vector>

class UISystem {
public:
    using UIGroupId = int;
    static constexpr UIGroupId kInvalidGroup = -1;

    // 泄漏式单例（见 UISystem.cpp）：控件析构会自注销，销毁顺序不得先于本系统
    static UISystem& Get();

    // ===== 组管理 =====
    UIGroupId CreateGroup(int layer);
    void SetGroupEnabled(UIGroupId id, bool enabled);
    bool IsGroupEnabled(UIGroupId id) const;
    void PushModal(UIGroupId id);   // 幂等：已在栈上则仅置顶
    void PopModal(UIGroupId id);    // 幂等：不在栈上则忽略
    bool IsModalActive() const { return !modalStack.empty(); }

    // ===== 控件注册（控件生命周期由持有方管理，本系统只存裸指针） =====
    void Add(UIGroupId groupId, UIWidget* widget);
    void Remove(UIWidget* widget);
    // 清交互态而不注销：场景/图层 Reset 时用（避免残留 hover/press 视觉）
    void ClearWidgetState(UIGroupId id);

    // ===== 每帧驱动（顺序固定：BeginFrame -> Dispatch -> [场景 Update/Render] -> EndFrame） =====
    void BeginFrame(float dt);
    void Dispatch();
    void EndFrame();

    // ===== 玩法输入推送（同在 Dispatch 中完成，在 UI 归属裁决之后） =====
    // 场景声明自己关注的键位：UI 系统不解释玩法语义，只按帧推送这些键的状态
    void WatchKeys(std::vector<int> keys);
    // 多播注册/注销玩法与 UI 快捷键接收者（场景 OnEnter 注册、OnExit 注销，避免跨场景推送）
    // 多个接收者可同时订阅（如 MainScene 玩法 + MainSceneUILayer 暂停快捷键）
    void AddGameInputSink(IGameInputSink* sink);
    void RemoveGameInputSink(IGameInputSink* sink);

    // ===== 对外查询（取代各处自写的"鼠标是否在按钮上"判定） =====
    bool IsPointerOverUI() const { return hovered != nullptr; }
    bool IsPointerCaptured() const { return captured != nullptr; }
    // 左键是否被 UI 占用：自按下起持续到抬起处理完毕，游戏侧据此放行/拦截输入
    bool IsButtonConsumed(int button) const { return (heldMask & (1u << button)) != 0u; }
    Vec2 GetPointer() const { return pointer; }
    UIWidget* GetHovered() const { return hovered; }
    UIWidget* GetPressed() const { return pressed; }
    UIWidget* GetCaptured() const { return captured; }

    UISystem(const UISystem&) = delete;
    UISystem& operator=(const UISystem&) = delete;

private:
    UISystem() = default;

    struct Group {
        int layer = 0;             // 组间层叠：越大越靠上
        int order = 0;             // 同 layer 时的创建序：越大越靠上
        bool enabled = false;      // 默认关闭，由所属场景/面板在状态变更处开启
        std::vector<UIWidget*> widgets;  // 注册顺序即层叠顺序（靠后者在上）
    };

    Group*       FindGroup(UIGroupId id);
    const Group* FindGroup(UIGroupId id) const;
    UIGroupId    GroupOf(const UIWidget* w) const;
    // 控件当前是否可参与命中：所属组启用，且未被更上层模态遮挡
    bool IsWidgetLive(const UIWidget* w) const;
    // 上一帧的目标本帧已失活（组关闭 / 模态压上 / 注销）时先行清理
    void CancelStaleTargets();
    // 整帧唯一一次命中判定
    UIWidget* PickHit(float px, float py) const;
    void Deliver(UIWidget* w, UIEventType type);
    // 将“未被 UI 消费的输入”打包推给玩法接收者
    void PushGameInput(bool leftPressEdge, bool leftDown, bool leftReleaseEdge);

private:
    std::vector<Group> groups;
    std::vector<UIGroupId> sortedGroups;  // 按 (layer, 创建序) 降序的组索引（仅创建组时重排）
    std::vector<UIGroupId> modalStack;
    int nextGroupOrder = 0;

    // 本帧输入缓存
    Vec2 pointer;
    Vec2 downPointer;
    float wheel = 0.0f;
    unsigned heldMask = 0;

    // 命中归属
    UIWidget* hovered = nullptr;
    UIWidget* pressed = nullptr;   // 本帧持有按下状态的控件（抬起时必回送它）
    UIWidget* captured = nullptr;  // 拖拽捕获中的控件

    // 玩法输入
    std::vector<int> watchedKeys;
    std::vector<IGameInputSink*> gameSinks;  // 多播：玩法与 UI 快捷键接收者均可订阅
};
