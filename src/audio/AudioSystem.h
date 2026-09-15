#pragma once

#include <string>
#include <vector>
#include <memory>
#include <random>
#include <stdexcept>
#include <miniaudio.h>

class AudioSystem
{
public:
    static AudioSystem& Instance();

    bool Init();
    void Shutdown();

    // ========= BGM =========
    bool PlayBGM(const std::string& file, bool loop = true);
    void StopBGM();
    void PauseBGM();
    void ResumeBGM();

    // ========= SFX =========
    void PlaySFX(const std::string& file, float volume = 1.0f);

    // ========= Ambient（环境音，如雨声：独立于 BGM，可循环） =========
    bool PlayAmbient(const std::string& file, bool loop = true, float volume = 1.0f);
    void StopAmbient();

    // ========= Volume =========
    void SetMasterVolume(float v);
    void SetBGMVolume(float v);
    void SetSFXVolume(float v);

    float GetMasterVolume() const { return m_masterVolume; }
    float GetBGMVolume() const { return m_bgmVolume; }
    float GetSFXVolume() const { return m_sfxVolume; }

    // ========= 持久化（读写 audio_settings.dat） =========
    void LoadSettings();
    void SaveSettings();

    // 每帧调用（清理已播放完成的音效）
    void Update();

private:
    AudioSystem() = default;
    ~AudioSystem() = default;

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

private:
    ma_engine m_engine{};
    bool m_initialized = false;

    // BGM
    std::unique_ptr<ma_sound> m_bgm;

    // Ambient（环境音，如雨声）
    std::unique_ptr<ma_sound> m_ambient;

    // SFX pool
    std::vector<std::unique_ptr<ma_sound>> m_sfxList;

    float m_masterVolume = 1.0f;
    float m_bgmVolume = 1.0f;
    float m_ambientVolume = 1.0f;
    float m_sfxVolume = 1.0f;
};

enum class SFX {
    Click,     // 按钮/通用点击（释放确认）
    Press,     // 按下瞬间轻量反馈
    Hover,     // 光标进入按钮：钝感轻提示
    Death,     // 碰撞死亡
    Fly,       // 振翅（鼠标/键盘）
    GameOver,  // 结算面板弹出
    Record,    // 破纪录
    Point,     // 穿过障碍物得分
    Collect,   // 收集道具
    Pause,       // 暂停
    Resume,      // 继续游戏
    Thunder,     // 雷击（短而强，与闪电视觉同步）
    Countdown,   // 3-2-1 倒数滴答（短促、较高频）
    Start        // 游戏正式开始（更高频上扫音）
};

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

inline const std::string& SFXToFile(SFX sfx) {
    static const std::string click = "assets/Audio/Sound Effects/Click.wav";
    static const std::string press = "assets/Audio/Sound Effects/Press.wav";
    static const std::string hover = "assets/Audio/Sound Effects/Hover.wav";
    static const std::string death = "assets/Audio/Sound Effects/Death.wav";
    static const std::string fly = "assets/Audio/Sound Effects/Fly.wav";
    static const std::string gameOver = "assets/Audio/Sound Effects/Game Over.wav";
    static const std::string record = "assets/Audio/Sound Effects/Record.wav";
    static const std::string point = "assets/Audio/Sound Effects/Point.wav";
    static const std::string collect = "assets/Audio/Sound Effects/Collect.wav";
    static const std::string pause = "assets/Audio/Sound Effects/Pause.wav";
    static const std::string resume = "assets/Audio/Sound Effects/Resume.wav";
    static const std::string countdown = "assets/Audio/Sound Effects/Countdown.wav";
    static const std::string start = "assets/Audio/Sound Effects/Start.wav";

    switch (sfx) {
        case SFX::Click: return click;
        case SFX::Press: return press;
        case SFX::Hover: return hover;
        case SFX::Death: return death;
        case SFX::Fly: return fly;
        case SFX::GameOver: return gameOver;
        case SFX::Record: return record;
        case SFX::Point: return point;
        case SFX::Collect: return collect;
        case SFX::Pause: return pause;
        case SFX::Resume: return resume;
        case SFX::Thunder: return RandomThunderFile();
        case SFX::Countdown: return countdown;
        case SFX::Start: return start;
        default: throw std::runtime_error("Unknown SFX");
    }
}