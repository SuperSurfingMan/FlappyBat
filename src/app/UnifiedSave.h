#pragma once
// =============================================================================
// 统一存档：金币、护盾科技树解锁进度（PlayerProfile）与成就信息（Achievements）
// 合并持久化到同一文件 player_save.dat（文本行式，行首标签区分归属）：
//   PlayerProfile 拥有标签：C（金币数）、L（升级轨道等级）
//   Achievements  拥有标签：A（已解锁成就 ID）、S（累计统计值）
//   AudioSystem   拥有标签：V（音量设置）
// 各系统保存时做「读-改-写」：只重写属于自己标签的行、保留其它系统的行，
// 互不覆盖且与保存顺序无关。旧版独立文件（profile.dat / achievements.dat /
// audio_settings.dat）仅在统一存档中尚无对应系统数据时作为一次性迁移来源读取，
// 之后不再使用。
// =============================================================================
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace UnifiedSave {

// 统一存档文件名（C++17 inline 变量：header-only 单头即可多处共享）
inline const char* const FileName = "player_save.dat";

// 读取指定文本文件的全部非空行（文件不存在返回空）
inline std::vector<std::string> ReadLinesFrom(const char* file) {
    std::vector<std::string> lines;
    std::ifstream in(file);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// 读取统一存档的全部行
inline std::vector<std::string> ReadAllLines() {
    return ReadLinesFrom(FileName);
}

// 行首 token（以空格结尾或整行即 token）是否命中标签集合之一
inline bool LineHasTag(const std::string& line, const std::vector<std::string>& tags) {
    for (const auto& t : tags) {
        if (line.compare(0, t.size(), t) == 0 &&
            (line.size() == t.size() || line[t.size()] == ' ')) {
            return true;
        }
    }
    return false;
}

// 读-改-写：保留统一存档中不属于 ownedTags 的行，替换为 newLines（本系统的新行）。
// 文件不存在时直接创建；任一系统先保存都不会丢另一系统的数据——另一系统
// 加载时有旧版独立文件的迁移兜底（见各系统 Load）。
inline void RewriteSection(const std::vector<std::string>& ownedTags,
                           const std::vector<std::string>& newLines) {
    std::vector<std::string> kept;
    for (const auto& line : ReadAllLines()) {
        if (!LineHasTag(line, ownedTags)) kept.push_back(line);
    }
    kept.insert(kept.end(), newLines.begin(), newLines.end());

    std::ofstream out(FileName, std::ios::trunc);
    if (!out) {
        std::cout << "[UnifiedSave] Failed to write " << FileName << std::endl;
        return;
    }
    for (const auto& line : kept) out << line << "\n";
}

} // namespace UnifiedSave
