#pragma once
#include "autoplay/IAutoPilotPolicy.h"
#include "gameplay/ModeDefinition.h"
#include <memory>
#include <vector>

// 内置策略登记点（AutoPilotRoster.cpp）：新增可自动演示模式的唯一登记处——
// 为该 ModeKey 注册一个 IAutoPilotPolicy 实现即可，本类与场景零改动。
void RegisterBuiltinPolicies(class AutoPilotDirector& director);

// =============================================================================
// Auto Play 导演：设置页开关 + 按模式键的策略注册表。
//
// 职责只有三件：
//   登记 —— RegisterPolicy 把策略挂到 ModeKey 上（classic 之外未登记 = 该模式
//           天然不受自动演示影响，"只在经典模式生效"由注册表保证）；
//   开关 —— Enabled 持久化到统一存档 U 行（读-改-写语义见 app/UnifiedSave.h），
//           构造即读档，与 TuningParams / 枭王形象批次同一约定；
//   决策 —— Armed（开关开 && 模式有策略）时把当帧感知转给策略，未武装不写
//           动作（MainScene 据此保持人类输入直通）。
//
// 本类不认识具体玩法：它只是"策略查找表 + 开关"，算法全部在 IAutoPilotPolicy
// 实现里（依赖倒置；当前仅策略注册表，无全局访问点，作为 Services 成员装配）。
// =============================================================================
class AutoPilotDirector {
public:
    AutoPilotDirector(); // 构造即从统一存档读开关偏好（U 行）

    // 注册策略：mode 的字面量须与 ModeRegistry 登记键一致（string_view 生命周期安全）
    void RegisterPolicy(ModeKey mode, std::shared_ptr<IAutoPilotPolicy> policy);

    // 设置页开关：切换即落盘（U 行），即时生效（下一帧 Decide 即见）
    void SetEnabled(bool on);
    bool Enabled() const { return enabled_; }

    // 武装判定：开关开启且当前模式登记了策略。MainScene 每帧先问这一位，
    // 未武装时不构造感知、不写动作，开销为零。
    bool IsArmed(ModeKey mode) const;

    // 决策：未武装时不动 out（调用方已置默认零值）；武装时用当前策略填充
    void Decide(ModeKey mode, const AutoPilotSenses& senses, float dt,
                AutoPilotActions& out) const;

private:
    const IAutoPilotPolicy* PolicyFor(ModeKey mode) const;

    struct Entry {
        ModeKey mode;
        std::shared_ptr<IAutoPilotPolicy> policy;
    };
    std::vector<Entry> policies_;
    bool enabled_ = false; // 默认关：存量玩家升级后行为不变

    void Load(); // 统一存档 U 行（"U 1" / "U 0"；缺行保持默认关）
};
