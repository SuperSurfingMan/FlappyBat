#pragma once
#include <cstddef>
#include <string_view>
#include <vector>

// =============================================================================
// 第 2 期：模式从「封闭枚举」升级为「策略对象」。
//
// ModeKey 是稳定的字符串标识（由注册表持有的字面量，string_view 生命周期安全）。
// 所有下游（菜单 / 排行榜 / 局内 / 怪物 / 成就）只认 IGameMode 接口与 ModeKey，
// 不再持有任何枚举值或下标 -> 枚举的隐含序号契约。
// 新增模式 = 新增一个 IGameMode 实现（或一条 ModeDefinition）+ 一行注册。
// =============================================================================
using ModeKey = std::string_view;

inline constexpr ModeKey kDefaultModeKey = "classic";

constexpr float kTimeRushDuration = 60.0f; // 限时冲刺时长（各模式 Reset 的倒计时初值）
constexpr int   kMaxDifficultyTier = 10;   // 难度档位上限

// 模式级数值差异（纯数据，WeatherDirector / RunFlow / StageHud 只读它）
struct ModeParams {
    float speedFactor;         // 障碍物速度倍率（乘在难度递进曲线上）
    int   scoreFactor;         // 得分倍率（风暴之夜双倍）
    float gustIntervalMin;     // 阵风间隔下限
    float gustIntervalMax;     // 阵风间隔上限
    float gustForceFactor;     // 阵风力度倍率
    bool  stormWeather;        // true = 固定天气（跳过难度档位曲线）
    float stormRain;           // 固定雨强（stormWeather 时）
    float stormLightningMin;   // 固定落雷间隔下限（stormWeather 时）
    float stormLightningMax;   // 固定落雷间隔上限（stormWeather 时）
    float stormScrim;          // 固定天色遮罩透明度（stormWeather 时）
    bool  timed;               // 是否限时
    float timeLimit;           // 限时秒数（timed=false 时仅作为 Reset 初值）
    float comboHudY;           // 连击 HUD 的 y（限时模式顶部让位给倒计时）
    float lightningWarnLead;   // 落雷预警提前量
};

// 计分时该模式要额外上报的成就事件（事件 key 由成就规则表消费）
struct ScoreAchievement {
    const char* eventKey;
    long long   value;
};

class IGameMode {
public:
    virtual ~IGameMode() = default;
    virtual ModeKey Key() const = 0;
    virtual const char* DisplayName() const = 0;
    virtual const char* MenuDesc() const = 0;
    virtual const ModeParams& Params() const = 0;
    // 单局得分变化时上报的成就事件。所有模式默认都上报通用 run_score；
    // 需要专属成就的模式在此追加，无需改动 MainScene。
    virtual std::vector<ScoreAchievement> ScoreAchievements(long long totalScore) const = 0;
};

// 纯数据的一条模式定义：新增常规模式只需在 ModeRoster.cpp 加一行
struct ModeDefinition {
    ModeKey     key;
    const char* displayName;
    const char* menuDesc;
    ModeParams  params;
    const char* scoreEventKey;  // nullptr = 无模式专属成就
};

// 数据驱动的默认策略实现；需要特殊行为的模式可直接实现 IGameMode
class DataDrivenMode : public IGameMode {
public:
    explicit DataDrivenMode(ModeDefinition def) : def_(def) {}

    ModeKey Key() const override { return def_.key; }
    const char* DisplayName() const override { return def_.displayName; }
    const char* MenuDesc() const override { return def_.menuDesc; }
    const ModeParams& Params() const override { return def_.params; }

    std::vector<ScoreAchievement> ScoreAchievements(long long total) const override {
        std::vector<ScoreAchievement> out;
        out.push_back({"run_score", total});
        if (def_.scoreEventKey && *def_.scoreEventKey)
            out.push_back({def_.scoreEventKey, total});
        return out;
    }

private:
    ModeDefinition def_;
};
