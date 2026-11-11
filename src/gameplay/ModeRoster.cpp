#include "gameplay/ModeRegistry.h"
#include <memory>

// =============================================================================
// 内置模式登记表（唯一的模式定义处）。
//
// 新增一个常规模式 = 在下面加一条 ModeDefinition；菜单按钮、排行榜页签与分段
// 存档、参数读取、成就上报全部由注册表自动派生，下游零改动。
// 需要特殊行为（自定义计分 / 自定义天气）时，另写一个 IGameMode 实现并在此
// Register，其余代码同样不用改。
//
// 注册序即 score.dat 的段索引：务必追加在末尾，切勿插入或重排，
// 否则旧存档的各段会被解释成别的模式。
// =============================================================================
namespace {

const ModeDefinition kBuiltinModes[] = {
    { "classic", "经典无尽", "穿越风暴, 挑战最高分",
      { 1.0f, 1, 7.0f, 13.0f, 1.0f, false, 0.0f, 0.0f, 0.0f, 0.0f,
        false, kTimeRushDuration, 60.0f, 1.6f },
      nullptr },

    { "time_rush", "限时冲刺", "60 秒冲分, 节奏更快",
      { 1.3f, 1, 7.0f, 13.0f, 1.0f, false, 0.0f, 0.0f, 0.0f, 0.0f,
        true,  kTimeRushDuration, 140.0f, 1.3f },
      "time_rush_score" },

    { "storm_night", "风暴之夜", "狂风与密集雷暴, 双倍得分的地狱模式",
      { 1.6f, 2, 3.0f, 6.0f, 1.3f, true, 1.0f, 1.8f, 3.0f, 0.32f,
        false, kTimeRushDuration, 60.0f, 1.0f },
      "storm_score" },
};

} // namespace

void RegisterBuiltinModes() {
    auto& registry = ModeRegistry::Instance();
    for (const auto& def : kBuiltinModes) {
        if (!registry.Contains(def.key))
            registry.Register(std::make_unique<DataDrivenMode>(def));
    }
}
