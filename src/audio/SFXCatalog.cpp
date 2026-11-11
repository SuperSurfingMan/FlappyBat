#include "audio/SFXCatalog.h"
#include "res/ManifestReader.h"

#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kManifestPath = "assets/manifests/sfx.txt";

// 内置路径表：顺序严格对应 SFX 枚举；manifest 缺失时的回退来源。
constexpr std::array<const char*, SFXCount> kBuiltinPaths = {{
    "assets/Audio/Sound Effects/Click.wav",       // Click
    "assets/Audio/Sound Effects/Press.wav",       // Press
    "assets/Audio/Sound Effects/Hover.wav",       // Hover
    "assets/Audio/Sound Effects/Death.wav",       // Death
    "assets/Audio/Sound Effects/Fly.wav",         // Fly
    "assets/Audio/Sound Effects/Game Over.wav",   // GameOver
    "assets/Audio/Sound Effects/Record.wav",      // Record
    "assets/Audio/Sound Effects/Point.wav",       // Point
    "assets/Audio/Sound Effects/Collect.wav",     // Collect
    "assets/Audio/Sound Effects/Pause.wav",       // Pause
    "assets/Audio/Sound Effects/Resume.wav",      // Resume
    "",                                           // Thunder（动态变体）
    "assets/Audio/Sound Effects/Countdown.wav",   // Countdown
    "assets/Audio/Sound Effects/Start.wav",       // Start
}};

constexpr std::array<const char*, SFXCount> kSfxKeys = {{
    "click", "press", "hover", "death", "fly", "gameover", "record",
    "point", "collect", "pause", "resume", "thunder", "countdown", "start",
}};

std::array<std::string, SFXCount> LoadPaths() {
    std::array<std::string, SFXCount> paths;
    for (std::size_t i = 0; i < SFXCount; ++i) paths[i] = kBuiltinPaths[i];

    std::vector<ManifestLine> lines;
    std::string error;
    if (!ReadManifest(kManifestPath, lines, &error)) {
        std::cout << "[SFXCatalog] " << error << "，回退内置路径表" << std::endl;
        return paths;
    }

    std::map<std::string, std::string> byKey;
    for (const auto& line : lines) byKey[line.key] = line.value;

    std::size_t overridden = 0;
    for (std::size_t i = 0; i < SFXCount; ++i) {
        auto it = byKey.find(kSfxKeys[i]);
        if (it != byKey.end() && !it->second.empty()) {
            paths[i] = it->second;
            ++overridden;
        }
    }
    std::cout << "[SFXCatalog] loaded from " << kManifestPath
              << " (" << overridden << " paths)" << std::endl;
    return paths;
}

const std::array<std::string, SFXCount>& SfxPaths() {
    static const std::array<std::string, SFXCount> paths = LoadPaths();
    return paths;
}

} // namespace

const std::string& SFXToFile(SFX sfx) {
    const std::size_t i = static_cast<std::size_t>(sfx);
    if (i >= SFXCount)
        throw std::out_of_range("SFXToFile: 未知 SFX（新增音效需同步补路径表）");
    if (sfx == SFX::Thunder) return RandomThunderFile();
    return SfxPaths()[i];
}
