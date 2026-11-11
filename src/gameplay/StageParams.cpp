#include "gameplay/StageParams.h"

#include <string>

// 第 2 期：模式参数不再是一张按枚举下标索引的静态表，而是由注册表里
// 各个 IGameMode 策略自己持有。新增模式不会让这里出现越界或漏配。
const ModeParams& ParamsOfIndex(std::size_t index) {
    return ModeRegistry::Instance().At(index).Params();
}

bool ValidateStageParams(std::string* firstError) {
    const auto fail = [firstError](const std::string& msg) {
        if (firstError) *firstError = msg;
        return false;
    };

    auto& registry = ModeRegistry::Instance();
    if (registry.Size() == 0)
        return fail("ModeRegistry 为空：组合根未调用 RegisterBuiltinModes()");

    for (std::size_t i = 0; i < registry.Size(); ++i) {
        const IGameMode& mode = registry.At(i);
        const ModeParams& p = mode.Params();
        const std::string where = "Mode[" + std::to_string(i) + " '" +
                                  std::string(mode.Key()) + "']";

        if (!mode.DisplayName() || !*mode.DisplayName())
            return fail(where + ": DisplayName 为空");
        if (!mode.MenuDesc() || !*mode.MenuDesc())
            return fail(where + ": MenuDesc 为空");
        if (p.speedFactor <= 0.0f)          return fail(where + ": speedFactor 必须为正");
        if (p.scoreFactor <= 0)             return fail(where + ": scoreFactor 必须为正");
        if (p.gustIntervalMin <= 0.0f || p.gustIntervalMax < p.gustIntervalMin)
                                            return fail(where + ": 阵风间隔区间非法");
        if (p.lightningWarnLead < 0.0f)     return fail(where + ": lightningWarnLead 不能为负");
        if (p.timed && p.timeLimit <= 0.0f) return fail(where + ": 限时模式必须有正的 timeLimit");
    }
    return true;
}
