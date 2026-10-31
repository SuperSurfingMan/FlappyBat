#pragma once

#include "../SpriteRenderer.h"
#include "SceneArgs.h"

class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;

    // 生命周期契约：
    // OnPrepare：仅经 SceneManager 切入时调用一次（Reset 之前），用于接收启动参数
    //            （如对局模式——参数须先于 Reset 到位，Reset 的初始布局依赖参数表）；
    //            局内重开等不经 SceneManager 的路径不会调用，既有状态得以保留。
    // OnEnter/OnExit：每次成为/离开当前场景时调用（订阅输入、启停 UI 组、音频）；
    // Reset：由 SceneManager 在 OnEnter 前调用，表示新一局/新会话的状态归零。
    virtual void OnPrepare(const SceneArgs& args) { (void)args; }
    virtual void OnEnter() = 0;
    virtual void OnExit() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render(SpriteRenderer& renderer) = 0;
    virtual void Reset() = 0;
};
