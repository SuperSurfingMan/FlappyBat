#pragma once
#include <fstream>
#include <string>
#include <vector>

// =============================================================================
// 轻量清单解析器（第 3 期：资源/音效清单外置）。
//
// 格式约定：
//   - `#` 开头为注释，空行忽略；
//   - `[section]` 切换段；
//   - 条目行 `key<TAB>value`，也允许只有 key（如 icons 段、空路径项）；
//   - 路径可含空格，但不能含 TAB。
//
// 刻意不引入 JSON 依赖：项目只需要「键 -> 路径」的扁平映射，
// 纯文本便于手工维护，也便于在没有第三方解析库时做失败回退。
// =============================================================================
struct ManifestLine {
    std::string section;
    std::string key;
    std::string value;
};

inline bool ReadManifest(const std::string& path, std::vector<ManifestLine>& out,
                         std::string* error = nullptr) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "无法打开 " + path;
        return false;
    }

    std::string section;
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const auto last = raw.find_last_not_of(" \t");
        const std::string line = raw.substr(first, last - first + 1);
        if (line[0] == '#') continue;

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            out.push_back({section, line, ""});
        } else {
            std::string value = line.substr(tab + 1);
            const auto vf = value.find_first_not_of(" \t");
            value = (vf == std::string::npos) ? "" : value.substr(vf);
            const auto vl = value.find_last_not_of(" \t");
            if (vl != std::string::npos) value = value.substr(0, vl + 1);
            out.push_back({section, line.substr(0, tab), value});
        }
    }
    if (error) error->clear();
    return true;
}
