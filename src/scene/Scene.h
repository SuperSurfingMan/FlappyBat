#pragma once

#include "../SpriteRenderer.h"

class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;

    // 生命周期契约：
    // OnEnter/OnExit：每次成为/离开当前场景时调用（订阅输入、启停 UI 组、音频）；
    // Reset：由 SceneManager 在 OnEnter 前调用，表示新一局/新会话的状态归零。
    virtual void OnEnter() = 0;
    virtual void OnExit() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render(SpriteRenderer& renderer) = 0;
    virtual void Reset() = 0;
};