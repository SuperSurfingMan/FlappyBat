#pragma once
#include "scene/Scene.h"
#include "scene/SceneKey.h"
#include "scene/SceneArgs.h"
#include "scene/ISceneTransition.h"
#include <functional>
#include <map>
#include <memory>

// =============================================================================
// SceneManager —— 场景注册表 + 声明式切场 + 转场编排。
//
// 职责边界：
//   1. 注册表：SceneKey -> 场景实例（由 Application 构造各场景后 Register）；
//   2. 切场：场景只发意图 RequestScene(SceneKey, args, style)，不再认识目标场景
//      的具体类型，也不再拼接「从全局取 App 再取场景」的样板；
//   3. 转场：持有一个 ISceneTransition 策略对象（本对象生命周期长于场景，
//      是"遮罩状态跨场景延续"的正确所有者）。帧循环经 TickTransition 驱动，
//      于 ReadyToSwitch 中点换景；覆盖层经 DrawTransitionOverlay 绘制在场景之后。
//      场景对具体转场无感知，只经 IsTransitionActive 查询全屏接管状态。
//
// 生命周期时序（换景一次）：
//   旧场景 OnExit -> 新场景 OnPrepare(args) -> Reset -> OnEnter。
//   OnPrepare 仅经本管理器切入时调用（注入启动参数）；局内重开等直接
//   Reset()+OnEnter() 的路径不会调用，模式等既有状态得以保留。
// =============================================================================
class SceneManager {
public:
    // 第 5 期：不再有 Instance() 静态访问点——本对象由组合根在栈上持有，
    // 与 main 同生命周期（比所有场景长，比服务单例短），引用经 Services 下发。
    SceneManager();
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // ===== 视口尺寸（组合根注入；仅用于转场默认聚焦锚点）=====
    void SetViewportSize(float w, float h) { viewportW_ = w; viewportH_ = h; }

    // ===== 场景注册表 =====
    void Register(SceneKey id, std::shared_ptr<Scene> scene);

    // ===== 转场工厂注册表（第 2 期）=====
    // 新增转场效果 = 新增一个 ISceneTransition 实现 + 组合根一行注册；
    // SceneManager 不再 include 任何具体转场类型，也没有样式 switch。
    using TransitionFactory = std::function<std::unique_ptr<ISceneTransition>()>;
    void RegisterTransitionFactory(TransitionKey style, TransitionFactory factory);

    // ===== 声明式切场：一行意图完成路由 + 参数注入 + 转场编排 =====
    // style == None：同帧同步直切（原 ChangeScene 语义，可在 Update/Render 中安全调用）；
    // 其它样式：启动转场，由 TickTransition 在遮蔽中点自动换景。
    // 转场进行中的新请求被忽略（防重入）。
    void RequestScene(SceneKey id, const SceneArgs& args = SceneArgs{},
                      TransitionKey style = kTransitionNone);

    // ===== 转场驱动（Application 帧循环调用；场景不再驱动/绘制任何转场） =====
    void TickTransition(float dt);      // 推进转场动画；中点执行换景
    void DrawTransitionOverlay();       // 场景 Render 之后调用：覆盖层追加到 ImGui 前景层
    bool IsTransitionActive() const;    // 全屏接管状态：输入闸门与场景查询共用

    // ===== 既有直切原语（Boot 初始进场等内部路径使用；args 缺省注入） =====
    void ChangeScene(std::shared_ptr<Scene> nextScene, const SceneArgs& args = SceneArgs{});
    std::shared_ptr<Scene> GetCurrentScene() const;
    // 退出当前场景（OnExit + 置空）：由 Application 在系统 Shutdown 前调用，
    // 确保 OnExit 执行时各单例（ScoreRank/PlayerProfile/Achievements 等）尚未析构
    void ExitCurrentScene();

private:
    // 换景执行体：OnExit(旧) -> OnPrepare -> Reset -> OnEnter（见类注释时序）
    void SwapScene(std::shared_ptr<Scene> nextScene, const SceneArgs& args);
    std::shared_ptr<Scene> Resolve(SceneKey id) const;
    std::unique_ptr<ISceneTransition> MakeTransition(TransitionKey style);

    std::shared_ptr<Scene> currentScene;

    // ===== 注册表与转场续态 =====
    std::map<SceneKey, std::shared_ptr<Scene>> registry;
    std::map<TransitionKey, TransitionFactory> transitionFactories;
    float viewportW_ = 0.0f;
    float viewportH_ = 0.0f;
    std::unique_ptr<ISceneTransition> transition; // 跨场景存活的转场策略（空 = 无转场）
    std::shared_ptr<Scene> pendingScene;          // ReadyToSwitch 时待换入的场景
    SceneArgs pendingArgs;                        // 及其启动参数
};
