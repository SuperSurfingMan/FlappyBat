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
// 控件所有权：
//   登记（Add）以 std::shared_ptr<UIWidget> 传入，本系统共享持有：注册期间控件必定存活，
//   无需依赖"析构自注销"避免悬垂登记。场景/图层想提前放弃某个控件请显式调 Remove。
//   hovered/pressed/captured 等帧级交互身份仍为裸指针（非拥有视图），因为登记期间
//   本系统必持有控件，这些视图不会悬垂；其有效性来自共享登记，不再来自调用方保证。
//
// 分组（Group）与层叠：
//   控件按组注册，组有 layer（越大越靠上）。命中解析时组间比 layer、组内比注册顺序
//   （后注册者在上），因此"上层面板的按钮"天然优先于"被它盖住的列表热区"。
//
// 模态（Modal）：
//   模态栈非空时，只有栈顶组的控件参与命中——下层组的所有控件自动失效，
//   无需再逐个 setActive(false) 挂起场景交互。
//   约束：调用方必须让 PushModal/PopModal 与 SetGroupEnabled 同帧成对，即
//   "在栈上"与"组启用"两个状态不允许分离。命中解析（PickHit）只看栈顶归属、
//   不回看组是否启用，与 IsWidgetLive 同一套判据；若压了栈却关掉组，本帧将没有任何
//   控件认领鼠标（下层组已被模态判为失活），表现为整块 UI 无响应而非命中抖动。
// =============================================================================
#include "ui/UIWidget.h"
#include "ui/GameInput.h"
#include <memory>
#include <vector>

class UISystem {
public:
    using UIGroupId = int;
    static constexpr UIGroupId kInvalidGroup = -1;

    // 泄漏式单例（见 UISystem.cpp）：登记表共享持有控件，且图层/场景析构时会回调用本系统注销 sink
    static UISystem& Get();

    // ===== 组管理 =====
    UIGroupId CreateGroup(int layer);
    void SetGroupEnabled(UIGroupId id, bool enabled);
    bool IsGroupEnabled(UIGroupId id) const;
    // 模态压栈/出栈：与 SetGroupEnabled(id, 同一条件) 同帧成对调用是硬性约束
    // （模态期间命中解析只看栈顶组，不会因栈顶组未启用而下沉到下层组）
    void PushModal(UIGroupId id);   // 幂等：已在栈上则仅置顶
    void PopModal(UIGroupId id);    // 幂等：不在栈上则忽略
    bool IsModalActive() const { return !modalStack.empty(); }

    // ===== 控件登记（共享所有权：登记即由本系统与持有方共同持有） =====
    void Add(UIGroupId groupId, const std::shared_ptr<UIWidget>& widget);
    void Remove(const std::shared_ptr<UIWidget>& widget);
    // 清交互态而不注销：场景/图层 Reset 时用（避免残留 hover/press 视觉）
    void ClearWidgetState(UIGroupId id);

    // ===== 每帧驱动（顺序固定：BeginFrame -> Dispatch -> [场景 Update/Render] -> EndFrame） =====
    void BeginFrame(float dt);
    void Dispatch();
    void EndFrame();

    // ===== 全屏交互闸门 =====
    // 转场遮罩等全屏接管期间由 Application 每帧同步（值取自 SceneManager::IsTransitionActive）：
    // true 时本帧一切控件交互冻结（悬停/按下/点击清空，走 Cancel 卫生路径）；
    // 玩法输入快照照常推送，是否忽略由场景按自身流程决定
    void SetInteractionBlocked(bool blocked) { interactionBlocked = blocked; }
    bool IsInteractionBlocked() const { return interactionBlocked; }

    // ===== 玩法输入推送（同在 Dispatch 中完成，在 UI 归属裁决之后） =====
    // 场景声明自己关注的键位：UI 系统不解释玩法语义，只按帧推送这些键的状态
    void WatchKeys(std::vector<int> keys);
    // 多播注册/注销玩法与 UI 快捷键接收者（场景 OnEnter 注册、OnExit 注销，避免跨场景推送）
    // 多个接收者可同时订阅（如 MainScene 玩法 + MainSceneUILayer 暂停快捷键）
    void AddGameInputSink(IGameInputSink* sink);
    void RemoveGameInputSink(IGameInputSink* sink);

    // ===== 对外查询（取代各处自写的"鼠标是否在按钮上"判定） =====
    // 下列 Get* 返回的裸指针为**非拥有视图**：指向登记表内的控件，调用方不得 delete，也不得跨帧缓存
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
        std::vector<std::shared_ptr<UIWidget>> widgets;  // 共享持有；注册顺序即层叠顺序（靠后者在上）
    };

    Group*       FindGroup(UIGroupId id);
    const Group* FindGroup(UIGroupId id) const;
    // 以下为非拥有指针参：仅用作登记表的身份查询键，不取得所有权
    UIGroupId    GroupOf(const UIWidget* w) const;
    // 控件当前是否可参与命中：所属组启用，且未被更上层模态遮挡
    bool IsWidgetLive(const UIWidget* w) const;
    // 上一帧的目标本帧已失活（组关闭 / 模态压上 / 注销）时先行清理
    void CancelStaleTargets();
    // 整帧唯一一次命中判定（返回登记表的非拥有指针，未命中为 nullptr）
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

    // 命中归属（非拥有：指向 groups[] 中已登记的控件，由 Dispatch / Remove / ClearWidgetState 同步维护）
    UIWidget* hovered = nullptr;
    UIWidget* pressed = nullptr;   // 本帧持有按下状态的控件（抬起时必回送它）
    UIWidget* captured = nullptr;  // 拖拽捕获中的控件

    // 玩法输入
    std::vector<int> watchedKeys;
    std::vector<IGameInputSink*> gameSinks;  // 多播：玩法与 UI 快捷键接收者均可订阅

    // 全屏交互闸门（每帧由 Application 依 SceneManager 状态同步）
    bool interactionBlocked = false;
};
