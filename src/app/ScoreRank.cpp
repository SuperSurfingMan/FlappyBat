#include "ScoreRank.h"
#include <fstream>
#include <algorithm>
#include <iostream>
// 新版文件魔数：文件首个 size_t 等于该值时按「三模式分段格式」解析；
// 否则视为旧版单榜格式（整文件按 size_t 顺序读入，全部归入经典模式）。
// 取现实中不可能达到的分数值，避免与旧数据的首条记录混淆。
static constexpr size_t kFileMagic = 0x46424154334D4F44ULL; // "FBAT3MOD"

ScoreRank::ScoreRank() {
    LoadFromFile();
}

const char* ScoreRank::ModeName(RankMode m) {
    static const char* names[ModeCount] = { "经典无尽", "限时冲刺", "风暴之夜" };
    const int i = (int)m;
    return (i >= 0 && i < (int)ModeCount) ? names[i] : "?";
}

std::vector<size_t> ScoreRank::GetScores(RankMode mode) {
    const int i = (int)mode;
    if (i < 0 || i >= (int)ModeCount) return {};
    return scoreRanks[i];
}

std::vector<size_t> ScoreRank::GetScoresFromDisk(RankMode mode) {
    std::cout << "score rank reload from disk" << std::endl;
    LoadFromFile();
    return GetScores(mode);
}

void ScoreRank::UpdateScore(size_t score, RankMode mode) {
    const int i = (int)mode;
    if (i < 0 || i >= (int)ModeCount) return;

    auto& list = scoreRanks[i];
    std::cout << "update score (" << ModeName(mode) << "): " << score << std::endl;
    list.emplace_back(score);
    // 降序排序：最高分排第一（排行榜 No.01 / 结算面板"最高分"均取 front()）
    std::sort(list.begin(), list.end(),
              [](size_t a, size_t b) { return a > b; });
    size_t actualSize = std::min(list.size(), listLengthLimit);
    list.assign(list.begin(), list.begin() + actualSize);
    WriteToFile();
}

void ScoreRank::ClearScores(RankMode mode) {
    const int i = (int)mode;
    if (i < 0 || i >= (int)ModeCount) return;
    // 一次性清空该模式内存榜单并立刻整文件重写（其它模式数据保留）
    scoreRanks[i].clear();
    WriteToFile();
    std::cout << "score rank cleared (" << ModeName(mode)
              << "), size: " << scoreRanks[i].size() << std::endl;
}

void ScoreRank::LoadFromFile() {
    for (auto& list : scoreRanks) list.clear();

    std::ifstream readScores(rankInfoFilename, std::ios::in | std::ios::binary);
    if (!readScores) return;

    size_t first = 0;
    if (readScores.read(reinterpret_cast<char*>(&first), sizeof(first)) && first == kFileMagic) {
        // 新版格式：[魔数][模式数][各模式: 条数 + 分数...]
        size_t modeCount = 0;
        readScores.read(reinterpret_cast<char*>(&modeCount), sizeof(modeCount));
        for (size_t m = 0; m < modeCount && m < ModeCount; ++m) {
            auto& list = scoreRanks[m];
            size_t cnt = 0;
            readScores.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));
            cnt = std::min(cnt, listLengthLimit);
            size_t number = 0;
            for (size_t k = 0; k < cnt &&
                 readScores.read(reinterpret_cast<char*>(&number), sizeof(number)); ++k) {
                list.emplace_back(number);
            }
        }
    } else {
        // 旧版单榜格式（无魔数头）：整文件顺序读入，一次性迁移为经典模式榜单
        auto& classic = scoreRanks[(size_t)RankMode::Classic];
        readScores.clear();
        readScores.seekg(0);
        size_t number = 0;
        while (classic.size() < listLengthLimit &&
               readScores.read(reinterpret_cast<char*>(&number), sizeof(number))) {
            classic.emplace_back(number);
        }
    }

    // 兼容旧数据文件（曾按升序写入）：读入后统一降序，保证榜首先为最高分
    for (auto& list : scoreRanks) {
        std::sort(list.begin(), list.end(),
                  [](size_t a, size_t b) { return a > b; });
    }
}

void ScoreRank::WriteToFile() {
    // 整文件重写：[魔数][模式数][各模式: 条数 + 分数...]，三种模式同文件分段保存
    std::ofstream outScores(rankInfoFilename, std::ios::out | std::ios::binary | std::ios::trunc);
    size_t magic = kFileMagic;
    size_t modeCount = ModeCount;
    outScores.write(reinterpret_cast<char*>(&magic), sizeof(magic));
    outScores.write(reinterpret_cast<char*>(&modeCount), sizeof(modeCount));
    for (size_t m = 0; m < ModeCount; ++m) {
        auto& list = scoreRanks[m];
        size_t cnt = std::min(list.size(), listLengthLimit);
        outScores.write(reinterpret_cast<char*>(&cnt), sizeof(cnt));
        if (cnt > 0) {
            outScores.write(reinterpret_cast<char*>(list.data()), sizeof(size_t) * cnt);
        }
    }
}
