#pragma once
#include <array>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// =============================================================================
// 音效目录：枚举 + 资源路径表。
//
// 原先与 miniaudio 引擎一起塞在 AudioSystem.h，导致「只想查一个音效路径」的
// 代码（以及数据表自测）也不得不 include 整个 miniaudio.h。现按职责拆开，
// AudioSystem.h 仍 include 本头，所有既有调用点无需改动。
//
// 映射改为按 SFX 值索引的常量表：新增音效只需在 Count 之前加枚举值 + 在表里
// 补一行路径；漏补会由启动期校验 / SFXToFile 的越界抛错明确暴露。
// =============================================================================
enum class SFX {
    Click,      // 按钮/通用点击（释放确认）
    Press,      // 按下瞬间轻量反馈
    Hover,      // 光标进入按钮：钝感轻提示
    Death,      // 碰撞死亡
    Fly,        // 振翅（鼠标/键盘）
    GameOver,   // 结算面板弹出
    Record,     // 破纪录
    Point,      // 穿过障碍物得分
    Collect,    // 收集道具
    Pause,      // 暂停
    Resume,     // 继续游戏
    Thunder,    // 雷击（随机变体，见 RandomThunderFile）
    Countdown,  // 3-2-1 倒数滴答
    Start,      // 游戏正式开始
    Count       // 哨兵，不是有效音效；必须永远留在最后
};

inline constexpr std::size_t SFXCount = static_cast<std::size_t>(SFX::Count);

// 雷声变体池：每次雷击随机选一个，避免重复感；线程安全的静态随机数
inline const std::string& RandomThunderFile() {
    static const std::vector<std::string> variants = {
        "assets/Audio/Sound Effects/Thunder.wav",
        "assets/Audio/Sound Effects/Thunder_1.wav",
        "assets/Audio/Sound Effects/Thunder_2.wav",
        "assets/Audio/Sound Effects/Thunder_3.wav",
        "assets/Audio/Sound Effects/Thunder_4.wav",
    };
    static thread_local std::mt19937 rng{std::random_device{}()};
    return variants[std::uniform_int_distribution<size_t>(
        0, variants.size() - 1)(rng)];
}

// 实现见 SFXCatalog.cpp：优先读取 assets/manifests/sfx.txt，
// 文件缺失或格式错误时回退到内置路径表。Thunder 始终由 RandomThunderFile 提供。
const std::string& SFXToFile(SFX sfx);
