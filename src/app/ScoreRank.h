#pragma once
#include "gameplay/ModeDefinition.h"
#include <cstddef>
#include <string>
#include <vector>

// =============================================================================
// 排行榜：按 ModeRegistry 的注册序动态分段，RankMode 枚举已移除。
//
// 存档协议保持不变：[魔数 "FBAT3MOD"][modeCount][各段: 条数 + 分数...]。
// 新增模式追加在注册序末尾即可：旧档 modeCount 较小时只读前几段，
// 新段为空；新档按当前 Size() 写出全部段。魔数绝不可改。
// =============================================================================
class ScoreRank
{
public:
    static ScoreRank& Get();
    static const char* ModeName(ModeKey key);

    std::vector<size_t> GetScores(ModeKey key);
    std::vector<size_t> GetScoresFromDisk(ModeKey key);
    void UpdateScore(size_t score, ModeKey key);
    void ClearScores(ModeKey key);

private:
    ScoreRank();
    void LoadFromFile();
    void WriteToFile();
    std::size_t IndexOf(ModeKey key) const; // 未登记返回 npos
    void EnsureSize();

    const std::string rankInfoFilename = "score.dat";
    // 各模式榜单保存上限：排行榜 UI 支持滚动浏览，需保留多于单屏（10 行）的历史记录
    const size_t listLengthLimit = 50;
    std::vector<std::vector<size_t>> scoreRanks;
};
