#pragma once
// =============================================================================
// 成就系统（B7）：header-only 单例，避免改动构建脚本。
// - 13 项成就：单局阈值类 / 累计类 / 模式专属类；每项 3 个等级（MaxLevel）
// - 等级结算：上报值/累计统计满足几档阈值即为几级（允许一次性跨级）
// - 持久化：统一存档 player_save.dat（与金币/护盾升级进度同文件，见 UnifiedSave.h；
//   本系统拥有行标签 A 成就达成等级（"A id level"，旧版 "A id" 无等级视为 1 级）/
//   S 累计统计值，UTF-8 名称留在内存；旧版 achievements.dat 仅作一次性迁移来源）
// - 升级后进入待提示队列（文案含等级），场景轮询 PopPendingToast 弹 ToastLayer
// 上报入口统一为 Notify(event, value)，由 MainScene 在各玩法节点调用。
// =============================================================================
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iostream>
#include <algorithm>

#include "app/UnifiedSave.h"

struct AchievementDef {
    int id;
    const char* name;
    const char* desc;      // 展示用描述（含三级阈值）
    long long tier[3];     // 三个等级阈值（依次递增）
};

// 事件标识改为字符串 key（第 2 期）：新增事件只需往 Rules() 加一条规则，
// 不再需要扩枚举、加 case。保留别名以兼容既有接口声明。
using AchievementEvent = std::string_view;

struct AchievementToast {
    int id;
    int level;
};

class Achievements {
public:
    enum { MaxLevel = 3 }; // 每个成就的等级数

    static Achievements& Get() {
        static Achievements instance;
        return instance;
    }

    static const std::vector<AchievementDef>& Defs() {
        static const std::vector<AchievementDef> defs = {
            {0,  "初试锋芒", "单局得分 10/25/40",           {10, 25, 40}},
            {1,  "渐入佳境", "单局得分 30/60/100",          {30, 60, 100}},
            {2,  "风暴行者", "单局得分 50/100/200",         {50, 100, 200}},
            {3,  "贴脸舞者", "单局贴脸 5/12/25 次",         {5, 12, 25}},
            {4,  "连击大师", "单局连击 5/10/20",            {5, 10, 20}},
            {5,  "巨人碰撞", "单局撞飞管道 3/8/15",         {3, 8, 15}},
            {6,  "大胃之王", "累计道具 50/150/400",         {50, 150, 400}},
            {7,  "长夜漫漫", "累计穿管道 200/600/1500",     {200, 600, 1500}},
            {8,  "引雷体质", "累计被雷击 3/10/25",          {3, 10, 25}},
            {9,  "分秒必争", "限时冲刺 40/100/180",         {40, 100, 180}},
            {10, "风暴之眼", "风暴之夜 30/80/140",          {30, 80, 140}},
            {11, "弑枭者",   "累计击杀枭王 1/5/15",         {1, 5, 15}},
            {12, "无伤弑枭", "满心弑枭累计 1/3/8 次",       {1, 3, 8}},
        };
        return defs;
    }

    // 当前达成等级（0 = 未达成，1~MaxLevel）
    int Level(int id) const {
        auto it = levels.find(id);
        return it == levels.end() ? 0 : it->second;
    }

    bool IsUnlocked(int id) const { return Level(id) >= 1; }

    // 至少达成 1 级的成就数
    int UnlockedCount() const { return (int)levels.size(); }

    // 全部成就的达成等级之和（等级总数）
    int TotalLevel() const {
        int n = 0;
        for (const auto& kv : levels) n += kv.second;
        return n;
    }

    // ===== 事件 -> 成就规则表（第 2 期：数据驱动，取代 switch 分发）=====
    // statKey != nullptr：累计型，先累加统计再按累计值结算；
    // statKey == nullptr：阈值型，直接用上报值结算。
    // 同一事件可对应多条规则（如 run_score 同时推进三个得分成就）。
    struct AchievementRule {
        const char* eventKey;
        int         achievementId;
        const char* statKey;
    };

    static const auto& Rules() {
        static const std::array rules = {
            AchievementRule{"pipe_passed",      7,  "pipes"},
            AchievementRule{"item_collected",   6,  "items"},
            AchievementRule{"lightning_death",  8,  "lightning"},
            AchievementRule{"run_score",        0,  nullptr},
            AchievementRule{"run_score",        1,  nullptr},
            AchievementRule{"run_score",        2,  nullptr},
            AchievementRule{"near_miss_run",    3,  nullptr},
            AchievementRule{"combo",            4,  nullptr},
            AchievementRule{"smash_run",        5,  nullptr},
            AchievementRule{"time_rush_score",  9,  nullptr},
            AchievementRule{"storm_score",     10,  nullptr},
            AchievementRule{"boss_kill",       11, "boss_kills"},
            AchievementRule{"boss_flawless",   12, "boss_flawless"},
        };
        return rules;
    }

    void Report(std::string_view eventKey, long long value) {
        for (const AchievementRule& r : Rules()) {
            if (eventKey != r.eventKey) continue;
            if (r.statKey) {
                stats[r.statKey] += value;
                CheckCumulative(r.achievementId, r.statKey);
            } else {
                CheckValue(r.achievementId, value);
            }
        }
    }

    // 弹出一个待提示的升级信息（"名称 Lv.X"），供场景每帧轮询
    bool PopPendingToast(AchievementToast& outToast) {
        if (pendingToasts.empty()) return false;
        outToast = pendingToasts.front();
        pendingToasts.erase(pendingToasts.begin());
        return true;
    }

    // ===== 持久化（统一存档 player_save.dat：A / S 行） =====
    void Load() {
        levels.clear();
        stats.clear();
        const auto lines = UnifiedSave::ReadAllLines();
        if (HasOwnData(lines)) {
            ParseLines(lines);
        } else {
            // 一次性迁移：统一存档中尚无本系统数据时，回退读取旧版 achievements.dat
            ParseLines(UnifiedSave::ReadLinesFrom("achievements.dat"));
        }
        // 用已加载的累计统计重结算等级：旧版存档的统计值可能已满足更高的新等级
        CheckCumulative(6, "items");
        CheckCumulative(7, "pipes");
        CheckCumulative(8, "lightning");
        CheckCumulative(11, "boss_kills");
        CheckCumulative(12, "boss_flawless");
    }

    void Save() const {
        std::vector<std::string> out;
        for (const auto& kv : levels) {
            out.push_back("A " + std::to_string(kv.first) + " " + std::to_string(kv.second));
        }
        for (const auto& kv : stats) {
            out.push_back("S " + kv.first + " " + std::to_string(kv.second));
        }
        UnifiedSave::RewriteSection({"A", "S"}, out); // 只重写 A/S 行，保留金币与升级部分
    }

private:
    Achievements() { Load(); }

    static bool HasOwnData(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            if (UnifiedSave::LineHasTag(line, {"A", "S"})) return true;
        }
        return false;
    }

    void ParseLines(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            std::istringstream iss(line);
            std::string tag;
            if (!(iss >> tag)) continue;
            if (tag == "A") {
                int id = 0;
                if (!(iss >> id)) continue;
                int lv = 1; // 旧版格式无等级字段，视为 1 级
                if (!(iss >> lv)) lv = 1;
                lv = std::clamp(lv, 1, (int)MaxLevel);
                auto it = levels.find(id);
                if (it == levels.end() || it->second < lv) levels[id] = lv; // 重复行取最高等级
            } else if (tag == "S") {
                std::string key;
                long long v = 0;
                if (iss >> key >> v) stats[key] = v;
            }
        }
    }

    static const AchievementDef* DefOf(int id) {
        for (const auto& d : Defs()) {
            if (d.id == id) return &d;
        }
        return nullptr;
    }

    static const char* NameOf(int id) {
        const AchievementDef* d = DefOf(id);
        return d ? d->name : "?";
    }

    // 满足的等级数：value 达到几档阈值即为几级
    static int SatisfiedTiers(const AchievementDef& d, long long value) {
        int n = 0;
        for (int t = 0; t < MaxLevel; ++t) {
            if (value >= d.tier[t]) ++n;
        }
        return n;
    }

    // 阈值类：根据上报值结算到最高满足等级
    void CheckValue(int id, long long value) {
        const AchievementDef* d = DefOf(id);
        if (!d) return;
        Advance(id, SatisfiedTiers(*d, value));
    }

    // 累计类：根据累计统计结算到最高满足等级
    void CheckCumulative(int id, const char* statKey) {
        auto it = stats.find(statKey);
        if (it == stats.end()) return;
        CheckValue(id, it->second);
    }

    // 推进等级：仅当新等级高于当前等级时生效（保存 + 进入待提示队列）
    void Advance(int id, int newLevel) {
        if (newLevel <= 0) return;
        newLevel = std::min(newLevel, (int)MaxLevel);
        int cur = Level(id);
        if (newLevel <= cur) return;
        levels[id] = newLevel;
        Save();
        pendingToasts.push_back({id, newLevel});
        std::cout << "[Achievement] " << NameOf(id) << " Lv." << newLevel << std::endl;
    }

    std::map<int, int> levels; // 成就 id -> 当前达成等级（1~MaxLevel）
    std::map<std::string, long long> stats;
    std::vector<AchievementToast> pendingToasts;
};
