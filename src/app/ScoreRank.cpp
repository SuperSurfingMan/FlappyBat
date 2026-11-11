#include "app/ScoreRank.h"
#include "gameplay/ModeRegistry.h"
#include <fstream>
#include <algorithm>
#include <iostream>

// 新版文件魔数：文件首个 size_t 等于该值时按「多模式分段格式」解析；
// 否则视为旧版单榜格式（整文件按 size_t 顺序读入，全部归入注册序第 0 个模式）。
//
// 注意：魔数里的 "3" 只是历史协议标识，不是模式数量。段数由文件中紧随其后的
// modeCount 字段表达；改动此值会让旧存档被误判为旧版单榜格式而损坏，务必保持不变。
static constexpr size_t kFileMagic = 0x46424154334D4F44ULL; // "FBAT3MOD"
static constexpr size_t kNoIndex = static_cast<size_t>(-1);

ScoreRank& ScoreRank::Get() {
    static ScoreRank instance;
    return instance;
}

ScoreRank::ScoreRank() {
    LoadFromFile();
}

void ScoreRank::EnsureSize() {
    const std::size_t n = ModeRegistry::Instance().Size();
    if (scoreRanks.size() < n) scoreRanks.resize(n);
}

std::size_t ScoreRank::IndexOf(ModeKey key) const {
    auto& registry = ModeRegistry::Instance();
    return registry.Contains(key) ? registry.IndexOf(key) : kNoIndex;
}

const char* ScoreRank::ModeName(ModeKey key) {
    auto& registry = ModeRegistry::Instance();
    return registry.Contains(key) ? registry.Get(key).DisplayName() : "?";
}

std::vector<size_t> ScoreRank::GetScores(ModeKey key) {
    const std::size_t i = IndexOf(key);
    if (i == kNoIndex) return {};
    EnsureSize();
    return scoreRanks[i];
}

std::vector<size_t> ScoreRank::GetScoresFromDisk(ModeKey key) {
    std::cout << "score rank reload from disk" << std::endl;
    LoadFromFile();
    return GetScores(key);
}

void ScoreRank::UpdateScore(size_t score, ModeKey key) {
    const std::size_t i = IndexOf(key);
    if (i == kNoIndex) return;
    EnsureSize();

    auto& list = scoreRanks[i];
    std::cout << "update score (" << ModeName(key) << "): " << score << std::endl;
    list.emplace_back(score);
    // 降序排序：最高分排第一（排行榜 No.01 / 结算面板"最高分"均取 front()）
    std::sort(list.begin(), list.end(),
              [](size_t a, size_t b) { return a > b; });
    size_t actualSize = std::min(list.size(), listLengthLimit);
    list.assign(list.begin(), list.begin() + actualSize);
    WriteToFile();
}

void ScoreRank::ClearScores(ModeKey key) {
    const std::size_t i = IndexOf(key);
    if (i == kNoIndex) return;
    EnsureSize();
    // 一次性清空该模式内存榜单并立刻整文件重写（其它模式数据保留）
    scoreRanks[i].clear();
    WriteToFile();
    std::cout << "score rank cleared (" << ModeName(key)
              << "), size: " << scoreRanks[i].size() << std::endl;
}

void ScoreRank::LoadFromFile() {
    // 段数始终与当前注册表对齐；注册表为空时不读档，等待组合根完成登记
    EnsureSize();
    if (scoreRanks.empty()) return; // 注册表为空时不读档，等待组合根完成登记
    for (auto& list : scoreRanks) list.clear();

    std::ifstream readScores(rankInfoFilename, std::ios::in | std::ios::binary);
    if (!readScores) return;

    size_t first = 0;
    if (readScores.read(reinterpret_cast<char*>(&first), sizeof(first)) && first == kFileMagic) {
        // 新版格式：[魔数][模式数][各模式: 条数 + 分数...]
        size_t modeCount = 0;
        readScores.read(reinterpret_cast<char*>(&modeCount), sizeof(modeCount));
        for (size_t m = 0; m < modeCount && m < scoreRanks.size(); ++m) {
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
        // 旧版单榜格式（无魔数头）：整文件顺序读入，迁移到注册序第 0 个模式
        auto& classic = scoreRanks[0];
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
    EnsureSize();
    // 整文件重写：[魔数][模式数][各模式: 条数 + 分数...]
    std::ofstream outScores(rankInfoFilename, std::ios::out | std::ios::binary | std::ios::trunc);
    size_t magic = kFileMagic;
    size_t modeCount = scoreRanks.size();
    outScores.write(reinterpret_cast<char*>(&magic), sizeof(magic));
    outScores.write(reinterpret_cast<char*>(&modeCount), sizeof(modeCount));
    for (size_t m = 0; m < scoreRanks.size(); ++m) {
        auto& list = scoreRanks[m];
        size_t cnt = std::min(list.size(), listLengthLimit);
        outScores.write(reinterpret_cast<char*>(&cnt), sizeof(cnt));
        if (cnt > 0) {
            outScores.write(reinterpret_cast<char*>(list.data()), sizeof(size_t) * cnt);
        }
    }
}
