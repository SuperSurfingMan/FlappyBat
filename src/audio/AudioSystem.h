#pragma once

#include <string>
#include <vector>
#include <memory>
#include <random>
#include <stdexcept>
#include <miniaudio.h>

#include "audio/SFXCatalog.h" // SFX 枚举 / 路径表（本头转包含，既有调用点无需改动）

class AudioSystem
{
public:
    static AudioSystem& Instance();

    bool Init();
    void Shutdown();

    // ========= BGM =========
    // fileGain：单曲响度补偿（不写入用户音量设置、不重编码文件）
    bool PlayBGM(const std::string& file, bool loop = true, float fileGain = 1.0f);
    void StopBGM();
    void PauseBGM();
    void ResumeBGM();
    // 当前 BGM 是否真的在出声（已加载且未暂停）。
    // 存在意义：暂停/恢复只是两次引擎调用，仅凭调用点无法判断是否真的静音，
    // 调试与自测需要可观测的音频状态。
    bool IsBGMPlaying() const;

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
    float m_bgmFileGain = 1.0f;
    float m_ambientVolume = 1.0f;
    float m_sfxVolume = 1.0f;
};

// SFX 枚举、雷声变体池与 SFXToFile 映射已拆到 audio/SFXCatalog.h：
// 音效目录（纯数据表）不再与 miniaudio 引擎声明捆绑，且改为按枚举值索引的
// 常量表 + 越界抛错，取代原先的 switch / 静默 default。