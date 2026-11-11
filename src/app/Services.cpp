#include "app/Services.h"

#include "app/Application.h"
#include "app/Achievements.h"
#include "app/PlayerProfile.h"
#include "app/ScoreRank.h"
#include "audio/AudioSystem.h"
#include "core/Config.h"
#include "res/ResourceManager.h"
#include "scene/SceneManager.h"
#include "ui/UISystem.h"

#include <iostream>

Services& Services::Get() {
    static Services instance;
    return instance;
}

void Services::Install() {
    Services& s = Get();
    // 显式、集中地触发各服务实例的构造。顺序即生命周期顺序：
    // 存档类（ScoreRank / Achievements / PlayerProfile）在读取 player_save.dat
    // 时彼此无依赖，但都必须在任何场景构造之前就位。
    s.cfg_     = &Config::GetInstance();
    s.audio_   = &AudioSystem::Instance();
    s.res_     = &ResourceManager::Get();
    s.scores_  = &ScoreRank::Get();
    s.ach_     = &Achievements::Get();
    s.profile_ = &PlayerProfile::Get();
    s.ui_      = &UISystem::Get();
    s.scenes_  = &SceneManager::Instance();
    s.app_     = &Application::Get();
    s.installed_ = true;
    std::cout << "[Services] installed (9 services)" << std::endl;
}

bool Services::IsInstalled() { return Get().installed_; }

Config&          Services::Cfg()     { return Require(cfg_); }
AudioSystem&     Services::Audio()   { return Require(audio_); }
ResourceManager& Services::Res()     { return Require(res_); }
ScoreRank&       Services::Scores()  { return Require(scores_); }
Achievements&    Services::Ach()     { return Require(ach_); }
PlayerProfile&   Services::Profile() { return Require(profile_); }
UISystem&        Services::UI()      { return Require(ui_); }
SceneManager&    Services::Scenes()  { return Require(scenes_); }
Application&     Services::App()     { return Require(app_); }
