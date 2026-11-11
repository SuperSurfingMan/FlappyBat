#pragma once
// =============================================================================
// 场景转场策略接口（对扩展开放）：新增一种转场效果 = 新增一个 ISceneTransition 实现
// ＋ 组合根一行工厂注册，场景与 SceneManager 的既有代码零改动。
//
// 第 4 期：转场样式从封闭枚举改为开放字符串键（与场景标识同构）。
// 新增样式不再需要改本头文件的任何枚举；空键表示「同帧直切、不建转场对象」。
//
// 职责边界：
//   - 实现类只负责"动画怎么播"与"覆盖层怎么画"（三相位状态机自包含）；
//   - 编排（何时换景、绘制次序、交互闸门、跨场景续态）由 SceneManager 统一负责；
//   - 场景对转场完全无感知，只经 SceneManager 查询"是否处于全屏接管状态"。
// =============================================================================
#include "core/Math.h"
#include <string>

// 转场样式键：RequestScene 按样式经工厂注册表取得实现
using TransitionKey = std::string;

// 空键 = 无样式（同帧同步直切，不创建转场对象）
inline constexpr const char* kTransitionNone        = "";
inline constexpr const char* kTransitionCircleFocus = "circleFocus";

// 一次转场的上下文：Start 时一次性捕获，后续不受场景切换影响
struct TransitionContext {
    // 聚焦锚点（屏幕坐标，Y 向下）：来自 SceneArgs::focusAnchorScreen，缺省为屏幕中心
    Vec2 focusAnchor{ 0.0f, 0.0f };
};

class ISceneTransition {
public:
    virtual ~ISceneTransition() = default;

    virtual void Start(const TransitionContext& ctx) = 0; // 开始一次转场（幂等：进行中重复调用无效）
    virtual void Update(float dt) = 0;                    // 推进动画（Application 帧循环统一驱动）
    virtual void Draw() = 0;                              // 覆盖层绘制（ImGui 前景层，场景 Render 之后）
    virtual bool ReadyToSwitch() const = 0;               // 中点：可以换景（遮蔽到位/最大时）
    virtual bool IsFinished() const = 0;                  // 揭示完毕，覆盖层可拆除
    virtual bool BlocksInteraction() const = 0;           // 是否屏蔽 UI 交互（全屏接管型为 true）
};
