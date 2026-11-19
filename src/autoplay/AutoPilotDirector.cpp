#include "autoplay/AutoPilotDirector.h"
#include "app/UnifiedSave.h"
#include <iostream>
#include <sstream>
#include <string>

// =============================================================================
// Auto Play 导演实现：注册表 + 开关存档 + 决策分发（契约见 AutoPilotDirector.h）
// =============================================================================

AutoPilotDirector::AutoPilotDirector() {
    Load();
}

void AutoPilotDirector::RegisterPolicy(ModeKey mode, std::shared_ptr<IAutoPilotPolicy> policy) {
    if (!policy) return;
    // 同键重注册按覆盖处理（便于测试/未来模式覆盖默认策略），登记序即查找序
    for (auto& e : policies_) {
        if (e.mode == mode) {
            e.policy = std::move(policy);
            return;
        }
    }
    policies_.push_back({mode, std::move(policy)});
}

void AutoPilotDirector::SetEnabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    UnifiedSave::RewriteSection({"U"}, {std::string("U ") + (on ? "1" : "0")}); // 只重写 U 行
    std::cout << "[AutoPlay] " << (on ? "enabled" : "disabled") << std::endl;
}

bool AutoPilotDirector::IsArmed(ModeKey mode) const {
    return enabled_ && PolicyFor(mode) != nullptr;
}

void AutoPilotDirector::Decide(ModeKey mode, const AutoPilotSenses& senses, float dt,
                               AutoPilotActions& out) const {
    const IAutoPilotPolicy* policy = PolicyFor(mode);
    if (!policy) return;
    out = policy->Decide(senses, dt);
}

const IAutoPilotPolicy* AutoPilotDirector::PolicyFor(ModeKey mode) const {
    for (const auto& e : policies_) {
        if (e.mode == mode) return e.policy.get();
    }
    return nullptr; // 该模式未登记策略：自动演示不生效（开闭点：Roster 加一行即扩展）
}

void AutoPilotDirector::Load() {
    for (const auto& line : UnifiedSave::ReadAllLines()) {
        if (!UnifiedSave::LineHasTag(line, {"U"})) continue;
        int v = -1;
        std::istringstream ss(line.size() > 2 ? line.substr(2) : std::string());
        if (ss >> v) enabled_ = (v == 1); // 非法值一律视为关，手改存档不会误开
        return; // 只认第一行 U
    }
}
