#pragma once
// =============================================================================
// 玩家档案（B8 子集：局内货币 + 能量护盾升级树）：header-only 单例，免改构建脚本。
// - 金币：局内拾取获得（蚊子/萤火虫 +1、猕猴桃 +2、巨化星 +5），结算时按 分数/5 追加
// - 护盾升级：两条轨道各 5 级
//     Duration 持续时间：3.0s + 0.4s/级（满级 5.0s）
//     Charge    充能速度：能量获取 ×(1.0 + 0.1/级)（满级 ×1.5）
// - 持久化：统一存档 player_save.dat（与成就信息同文件，见 UnifiedSave.h；
//   本系统拥有行标签 C 金币数 / L 轨道 等级；旧版 profile.dat 仅作一次性迁移来源）
// 金币变更不自动存盘（拾取频繁），由局末结算/退出/购买时统一 Save。
// =============================================================================
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/UnifiedSave.h"

class PlayerProfile {
public:
    static PlayerProfile& Get() {
        static PlayerProfile instance;
        return instance;
    }

    // ===== 金币 =====
    long long GetCoins() const { return coins; }
    void AddCoins(long long n) { coins += n; }
    bool TrySpend(long long n) {
        if (coins < n) return false;
        coins -= n;
        Save();
        return true;
    }

    // ===== 护盾升级轨道 =====
    enum ShieldTrack { Duration = 0, Charge = 1 };
    static constexpr int TrackCount = 2;
    static constexpr int MaxLevel = 5;
    // 两条轨道共用同一价格曲线
    static constexpr long long UpgradeCosts[MaxLevel] = { 60, 140, 280, 480, 760 };

    int GetLevel(int track) const {
        if (track < 0 || track >= TrackCount) return 0;
        return shieldLevel[track];
    }

    long long NextCost(int track) const {
        int lv = GetLevel(track);
        if (lv >= MaxLevel) return -1; // 已满级
        return UpgradeCosts[lv];
    }

    bool TryBuy(int track) {
        int lv = GetLevel(track);
        if (lv >= MaxLevel) return false;
        if (!TrySpend(UpgradeCosts[lv])) return false;
        shieldLevel[track] = lv + 1;
        std::cout << "[Profile] Shield track " << track << " upgraded to level " << lv + 1 << std::endl;
        return true;
    }

    // ===== 持久化（统一存档 player_save.dat：C / L 行） =====
    void Load() {
        const auto lines = UnifiedSave::ReadAllLines();
        if (HasOwnData(lines)) {
            ParseLines(lines);
        } else {
            // 一次性迁移：统一存档中尚无本系统数据时，回退读取旧版 profile.dat
            ParseLines(UnifiedSave::ReadLinesFrom("profile.dat"));
        }
    }

    void Save() const {
        std::vector<std::string> out;
        out.push_back("C " + std::to_string(coins));
        for (int t = 0; t < TrackCount; ++t) {
            out.push_back("L " + std::to_string(t) + " " + std::to_string(shieldLevel[t]));
        }
        UnifiedSave::RewriteSection({"C", "L"}, out); // 只重写 C/L 行，保留成就部分
    }

private:
    PlayerProfile() { Load(); }

    static bool HasOwnData(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            if (UnifiedSave::LineHasTag(line, {"C", "L"})) return true;
        }
        return false;
    }

    void ParseLines(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            std::istringstream iss(line);
            char tag = 0;
            if (!(iss >> tag)) continue;
            if (tag == 'C') {
                iss >> coins;
            } else if (tag == 'L') {
                int track = 0, lv = 0;
                if (iss >> track >> lv && track >= 0 && track < TrackCount) {
                    shieldLevel[track] = std::clamp(lv, 0, MaxLevel);
                }
            }
        }
    }

    long long coins = 0;
    int shieldLevel[TrackCount] = { 0, 0 };
};
