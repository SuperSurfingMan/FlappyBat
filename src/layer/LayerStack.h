#pragma once
#include "Layer.h"
#include <memory>
#include <vector>

// 场景内屏幕覆盖层的组合容器。
//
// 边界：
//   - 只装 UI / 结算 / Toast 这类屏幕层；雨、闪电、风、怪物等世界渲染仍由
//     Scene::Render 显式排序（雨的 4 个 depth band 需要穿插在世界元素之间）。
//   - 插入顺序 = Tick/Draw 顺序；Draw 再按 RenderPhase 分成 HUD 前/后两段。
//   - Reset 不在这里统一调用：层的重置语义并不一致（局内 UI 随局重置，
//     成就 Toast 可跨局保留），由 Scene 显式决定重置哪些层。
class LayerStack {
public:
    void Add(const std::shared_ptr<Layer>& layer) {
        if (!layer) return;
        layers_.push_back(layer);
        if (attached_) layer->Attach();
    }

    void AttachAll() {
        if (attached_) return;
        attached_ = true;
        for (auto& layer : layers_) layer->Attach();
    }

    void DetachAll() {
        if (!attached_) return;
        // 反向卸载：后挂载的先释放，UI 组/模态按 LIFO 顺序清理
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            (*it)->Detach();
        }
        attached_ = false;
    }

    void Tick(float dt) {
        for (auto& layer : layers_) layer->Tick(dt);
    }

    void Draw(SpriteRenderer& renderer, LayerRenderPhase phase) {
        for (auto& layer : layers_) {
            if (layer->RenderPhase() == phase) layer->Draw(renderer);
        }
    }

    // 场景退出/输入整体失效时的兜底开关；正常状态由各层自己的业务门控维护。
    void SetInputEnabled(bool on) {
        for (auto& layer : layers_) layer->SetInputEnabled(on);
    }

    bool IsAttached() const { return attached_; }

private:
    std::vector<std::shared_ptr<Layer>> layers_;
    bool attached_ = false;
};
