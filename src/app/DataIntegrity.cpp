#include "app/DataIntegrity.h"

#include "app/Achievements.h"
#include "app/ScoreRank.h"
#include "audio/SFXCatalog.h"
#include "enemy/EnemyDirector.h"
#include "gameplay/ItemCatalog.h"
#include "gameplay/ModeRegistry.h"
#include "gameplay/StageParams.h"

#include <exception>
#include <set>
#include <string>

bool ValidateDataTables(std::string* firstError) {
    const auto fail = [firstError](const std::string& msg) {
        if (firstError) *firstError = msg;
        return false;
    };

    // 1) 模式参数表：每个模式都要有有效数值与非空展示文案
    if (!ValidateStageParams(firstError)) return false;

    // 2) 排行榜段与注册表同源：ModeName 直接向注册表取名字，无第二份枚举
    {
        auto& registry = ModeRegistry::Instance();
        for (const IGameMode* mode : registry.All()) {
            const char* rankName = ScoreRank::ModeName(mode->Key());
            if (!rankName || !*rankName || std::string(rankName) == "?")
                return fail("ScoreRank::ModeName['" + std::string(mode->Key()) + "'] 缺失");
            if (std::string(rankName) != std::string(mode->DisplayName()))
                return fail("ScoreRank::ModeName['" + std::string(mode->Key()) +
                            "'] 与 DisplayName 不一致");
        }
    }

    // 3) 音效路径表：每个枚举值都要能解析到非空路径
    for (std::size_t i = 0; i < SFXCount; ++i) {
        const SFX s = static_cast<SFX>(i);
        try {
            if (SFXToFile(s).empty())
                return fail("SFXToFile[" + std::to_string(i) + "] 路径为空（漏补路径表？）");
        } catch (const std::exception& e) {
            return fail(std::string("SFXToFile[") + std::to_string(i) + "]: " + e.what());
        }
    }

    // 4) 成就定义：id 唯一、文案非空、三级阈值严格递增
    {
        std::set<int> ids;
        for (const auto& d : Achievements::Defs()) {
            if (!ids.insert(d.id).second)
                return fail("AchievementDef id 重复: " + std::to_string(d.id));
            if (!d.name || !*d.name || !d.desc || !*d.desc)
                return fail("AchievementDef[" + std::to_string(d.id) + "] 文案为空");
            for (int t = 0; t < Achievements::MaxLevel; ++t) {
                if (d.tier[t] <= 0 || (t > 0 && d.tier[t] <= d.tier[t - 1]))
                    return fail("AchievementDef[" + std::to_string(d.id) + "] 阈值未严格递增");
            }
        }
    }

    // 5) 道具定义表：键 / 资源路径 / 动画参数 / 飘字 / 权重完整，且权重和为 1
    {
        float weightSum = 0.0f;
        for (std::size_t i = 0; i < ItemCount(); ++i) {
            const ItemDefinition& d = ItemAt(i);
            const std::string where = "ItemCatalog[" + std::to_string(i) + "]";
            if (!d.key || !*d.key || !d.poolKey || !*d.poolKey)
                return fail(where + ": 键为空");
            if (!d.texturePath || !*d.texturePath ||
                !d.disappearPath || !*d.disappearPath)
                return fail(where + ": 贴图路径为空");
            if (d.rows <= 0 || d.cols <= 0 || d.disappearFrames <= 0)
                return fail(where + ": 动画参数非法");
            if (!d.effect.floatText || !*d.effect.floatText)
                return fail(where + ": 飘字为空");
            if (d.weightNormal < 0.0f || d.weightHighTier < 0.0f)
                return fail(where + ": 生成权重不能为负");
            weightSum += d.weightNormal;
        }
        if (weightSum <= 0.0f) return fail("ItemCatalog: 普通难度权重总和为 0");
    }

    // 6) 怪物按模式调参表：新增模式时最容易漏补的隐藏成本，统一在此拦截
    if (!ValidateEnemyTunings(firstError)) return false;

    return true;
}
