#pragma once
#include <array>
#include <string>
#include <vector>

class ScoreRank
{
public:
    // ===== 模式分类：三个游戏模式的分数排行独立计算 =====
    // 与 MainScene::GameMode 一一对应（经典无尽 / 限时冲刺 / 风暴之夜）
    enum class RankMode { Classic = 0, TimeRush = 1, StormNight = 2 };
    static constexpr size_t ModeCount = 3;
    static const char* ModeName(RankMode m);

    static ScoreRank& Get() {
        static ScoreRank instance;
        return instance;
    }

    std::vector<size_t> GetScores(RankMode mode);
    std::vector<size_t> GetScoresFromDisk(RankMode mode);
    void UpdateScore(size_t score, RankMode mode);
    void ClearScores(RankMode mode);

private:
    const std::string rankInfoFilename = "score.dat";
    // 各模式榜单保存上限：排行榜 UI 支持滚动浏览，需保留多于单屏（10 行）的历史记录
    const size_t listLengthLimit = 50;
    // 各模式独立榜单（下标 = RankMode）
    std::array<std::vector<size_t>, ModeCount> scoreRanks;

private:
    ScoreRank();
    void LoadFromFile();
    void WriteToFile();
};
