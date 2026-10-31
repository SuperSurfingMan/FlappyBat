#pragma once
#include "render/SpriteRenderer.h"

// 屏幕覆盖层的渲染阶段：世界与场景 HUD 的绘制顺序由 Scene 显式控制，
// 层只按阶段插入，避免把雨的深度分层等世界渲染强行塞进 LayerStack。
enum class LayerRenderPhase {
    BeforeHud, // 场景 HUD 之前（如 ImGui 前景成就提示）
    AfterHud   // 场景 HUD 之后（普通 UI / 结算面板）
};

class Layer {
public:
    virtual ~Layer() = default;

    // ===== 挂载/卸载：由 LayerStack 在场景进入/离开时调用 =====
    // 输入订阅、UI 组注册/注销等生命周期动作放在 OnAttach/OnDetach，
    // 场景不再需要记住"哪个层要订阅什么"。
    void Attach() {
        if (attached_) return;
        attached_ = true;
        OnAttach();
    }

    void Detach() {
        if (!attached_) return;
        OnDetach();
        attached_ = false;
    }

    bool IsAttached() const { return attached_; }

    // ===== 三个门控：更新、渲染、命中 =====
    // 具体层不再各自写 if (!active) return；场景只改状态，遍历由 LayerStack 完成。
    void SetUpdateEnabled(bool on) { updateEnabled_ = on; }
    void SetRenderEnabled(bool on) { renderEnabled_ = on; }

    void SetInputEnabled(bool on) {
        if (inputEnabled_ == on) return;
        inputEnabled_ = on;
        OnInputEnabledChanged(on);
    }

    bool UpdateEnabled() const { return updateEnabled_; }
    bool RenderEnabled() const { return renderEnabled_; }
    bool InputEnabled() const { return inputEnabled_; }

    // 业务态（如暂停）变化后，用当前输入门控重新同步 UI 组/模态。
    void RefreshInput() { OnInputEnabledChanged(inputEnabled_); }

    // ===== 帧驱动：门控检查后转发到 protected 钩子 =====
    void Tick(float dt) {
        if (updateEnabled_) OnUpdate(dt);
    }

    void Draw(SpriteRenderer& renderer) {
        if (renderEnabled_) OnDraw(renderer);
    }

    virtual void Reset() = 0;

    // 默认在场景 HUD 之后绘制；需要压在前景提示层的覆盖层可覆盖。
    virtual LayerRenderPhase RenderPhase() const { return LayerRenderPhase::AfterHud; }

protected:
    virtual void OnUpdate(float dt) = 0;
    virtual void OnDraw(SpriteRenderer& renderer) = 0;
    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnInputEnabledChanged(bool) {}

private:
    bool attached_ = false;
    bool updateEnabled_ = true;
    bool renderEnabled_ = true;
    bool inputEnabled_ = true;
};
