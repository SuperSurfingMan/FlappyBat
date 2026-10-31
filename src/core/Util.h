#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <memory>

inline bool FileExists(const std::string& filePath)
{
    std::filesystem::path path(filePath);
    // use filesystem to check if the path exists and is a regular file
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

inline std::vector<std::string> ListFilesInFolder(const std::string& folderPath) {
    // 这里使用 C++17 的文件系统库
    std::vector<std::string> filePaths;
    std::filesystem::path path(folderPath);

    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                filePaths.push_back(entry.path().string());
            }
        }
    }

    return filePaths;
}