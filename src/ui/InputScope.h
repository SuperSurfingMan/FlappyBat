#pragma once
#include "ui/UISystem.h"
#include <vector>

// 把"组启用"与"模态压栈/弹栈"绑定成一个原子操作。
//
// 项目硬性约束：PushModal/PopModal 必须与 SetGroupEnabled 同帧成对，
// 否则会出现"模态栈非空但栈顶组未启用"的整块 UI 无响应状态。
// 场景/图层只需要声明自己绑定哪些组、哪些是模态，不再手写配对逻辑。
class InputScope {
public:
    void Bind(UISystem::UIGroupId id, bool modal) {
        bindings_.push_back(Binding{id, modal});
        Apply(bindings_.back());
    }

    void SetEnabled(bool on) {
        if (initialized_ && enabled_ == on) return;
        initialized_ = true;
        enabled_ = on;
        for (auto& binding : bindings_) Apply(binding);
    }

    bool IsEnabled() const { return enabled_; }

private:
    struct Binding {
        UISystem::UIGroupId id;
        bool modal;
    };

    void Apply(const Binding& binding) const {
        UISystem& ui = UISystem::Get();
        if (enabled_) {
            // 先启用组、再压栈：保证模态在栈上时组已经是可命中的
            ui.SetGroupEnabled(binding.id, true);
            if (binding.modal) ui.PushModal(binding.id);
        } else {
            // 先弹栈、再禁用组：避免"组仍启用但已不在模态栈"的过渡态
            if (binding.modal) ui.PopModal(binding.id);
            ui.SetGroupEnabled(binding.id, false);
        }
    }

    std::vector<Binding> bindings_;
    bool enabled_ = false;
    bool initialized_ = false;
};
