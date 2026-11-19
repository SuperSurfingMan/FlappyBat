#include "audio/AudioSystem.h"
#include "app/UnifiedSave.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

AudioSystem& AudioSystem::Instance()
{
    static AudioSystem instance;
    return instance;
}

bool AudioSystem::Init()
{
    if (m_initialized)
        return true;

    if (ma_engine_init(nullptr, &m_engine) != MA_SUCCESS)
    {
        std::cout << "Failed to init audio engine\n";
        return false;
    }

    ma_engine_set_volume(&m_engine, m_masterVolume);

    m_initialized = true;

    // 加载持久化的音量设置（覆盖默认值并应用到引擎）
    LoadSettings();

    return true;
}

void AudioSystem::Shutdown()
{
    if (!m_initialized)
        return;

    for (auto &sound : m_sfxList)
    {
        if (sound)
            ma_sound_uninit(sound.get());
    }
    m_sfxList.clear();

    if (m_bgm)
    {
        ma_sound_uninit(m_bgm.get());
        m_bgm.reset();
    }

    if (m_ambient)
    {
        ma_sound_uninit(m_ambient.get());
        m_ambient.reset();
    }

    ma_engine_uninit(&m_engine);
    m_initialized = false;
}


bool AudioSystem::PlayBGM(const std::string& file, bool loop, float fileGain)
{
    if (!m_initialized) return false;

    StopBGM();
    m_bgmFileGain = fileGain;

    m_bgm = std::make_unique<ma_sound>();

    if (ma_sound_init_from_file(
            &m_engine,
            file.c_str(),
            MA_SOUND_FLAG_STREAM,
            nullptr,
            nullptr,
            m_bgm.get()) != MA_SUCCESS)
    {
        m_bgm.reset();
        return false;
    }

    ma_sound_set_looping(m_bgm.get(), loop);
    ma_sound_set_volume(m_bgm.get(), m_bgmVolume * m_masterVolume * m_bgmFileGain);
    ma_sound_start(m_bgm.get());

    return true;
}

void AudioSystem::StopBGM()
{
    if (!m_bgm) return;

    ma_sound_uninit(m_bgm.get());
    m_bgm.reset();
}

void AudioSystem::PauseBGM()
{
    if (!m_bgm) return;
    // ma_sound_stop 保留播放游标，后续 ResumeBGM 可从原位置续播（区别于 StopBGM 的销毁重载）
    ma_sound_stop(m_bgm.get());
    std::cout << "[Audio] BGM paused" << std::endl;
}

void AudioSystem::ResumeBGM()
{
    if (!m_bgm) return;
    ma_sound_start(m_bgm.get());
    std::cout << "[Audio] BGM resumed" << std::endl;
}

bool AudioSystem::IsBGMPlaying() const
{
    return m_bgm && ma_sound_is_playing(m_bgm.get()) != MA_FALSE;
}

void AudioSystem::PlaySFX(const std::string& file, float volume)
{
    if (!m_initialized) {
        std::cerr << "[Audio] PlaySFX: engine not initialized: " << file << "\n";
        return;
    }

    auto sound = std::make_unique<ma_sound>();

    if (ma_sound_init_from_file(
            &m_engine,
            file.c_str(),
            0,
            nullptr,
            nullptr,
            sound.get()) != MA_SUCCESS)
    {
        std::cerr << "[Audio] PlaySFX: failed to load " << file << "\n";
        return;
    }

    ma_sound_set_volume(
        sound.get(),
        volume * m_sfxVolume * m_masterVolume);

    ma_sound_start(sound.get());

    m_sfxList.push_back(std::move(sound));
}

bool AudioSystem::PlayAmbient(const std::string& file, bool loop, float volume)
{
    if (!m_initialized) return false;

    StopAmbient();

    m_ambient = std::make_unique<ma_sound>();

    if (ma_sound_init_from_file(
            &m_engine,
            file.c_str(),
            MA_SOUND_FLAG_STREAM,
            nullptr,
            nullptr,
            m_ambient.get()) != MA_SUCCESS)
    {
        m_ambient.reset();
        return false;
    }

    ma_sound_set_looping(m_ambient.get(), loop);
    ma_sound_set_volume(m_ambient.get(), volume * m_ambientVolume * m_masterVolume);
    ma_sound_start(m_ambient.get());

    return true;
}

void AudioSystem::StopAmbient()
{
    if (!m_ambient) return;

    ma_sound_uninit(m_ambient.get());
    m_ambient.reset();
}

void AudioSystem::Update()
{
    m_sfxList.erase(
        std::remove_if(
            m_sfxList.begin(),
            m_sfxList.end(),
            [](const std::unique_ptr<ma_sound>& s)
            {
                if (!ma_sound_is_playing(s.get()))
                {
                    ma_sound_uninit(s.get());
                    return true;
                }
                return false;
            }),
        m_sfxList.end());
}

void AudioSystem::SetMasterVolume(float v)
{
    m_masterVolume = v;
    ma_engine_set_volume(&m_engine, v);
}

void AudioSystem::SetBGMVolume(float v)
{
    m_bgmVolume = v;
    if (m_bgm)
        ma_sound_set_volume(m_bgm.get(), v * m_masterVolume * m_bgmFileGain);
}

void AudioSystem::SetSFXVolume(float v)
{
    m_sfxVolume = v;
}

void AudioSystem::LoadSettings()
{
    // 统一存档：V 标签行 = "V <master> <bgm> <sfx>"（AudioSystem 专属标签，见 app/UnifiedSave.h）
    for (const auto& line : UnifiedSave::ReadAllLines())
    {
        if (line.rfind("V ", 0) != 0)
            continue;

        std::istringstream ss(line.substr(2));
        float master = 0.0f, bgm = 0.0f, sfx = 0.0f;
        if (ss >> master >> bgm >> sfx)
        {
            // 限定到合理范围后应用
            if (master >= 0.0f && master <= 1.0f) SetMasterVolume(master);
            if (bgm >= 0.0f && bgm <= 1.0f)       SetBGMVolume(bgm);
            if (sfx >= 0.0f && sfx <= 1.0f)       SetSFXVolume(sfx);
            return;
        }
    }

    // 一次性迁移：统一存档尚无音量数据时，读旧版独立二进制 audio_settings.dat，
    // 成功后立即写入统一存档，此后旧文件不再读取（与 profile.dat/achievements.dat 同策略）
    std::ifstream f("audio_settings.dat", std::ios::binary);
    if (!f) return;

    float master, bgm, sfx;
    if (f.read(reinterpret_cast<char*>(&master), sizeof(float)) &&
        f.read(reinterpret_cast<char*>(&bgm), sizeof(float)) &&
        f.read(reinterpret_cast<char*>(&sfx), sizeof(float)))
    {
        if (master >= 0.0f && master <= 1.0f) SetMasterVolume(master);
        if (bgm >= 0.0f && bgm <= 1.0f)       SetBGMVolume(bgm);
        if (sfx >= 0.0f && sfx <= 1.0f)       SetSFXVolume(sfx);
        SaveSettings();
    }
}

void AudioSystem::SaveSettings()
{
    std::ostringstream ss;
    ss << "V " << m_masterVolume << " " << m_bgmVolume << " " << m_sfxVolume;
    UnifiedSave::RewriteSection({"V"}, {ss.str()});
}
