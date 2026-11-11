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

Services::Services(Config& cfg,
                   AudioSystem& audio,
                   ResourceManager& res,
                   ScoreRank& scores,
                   Achievements& ach,
                   PlayerProfile& profile,
                   UISystem& ui,
                   SceneManager& scenes,
                   Application& app)
    : cfg_(&cfg),
      audio_(&audio),
      res_(&res),
      scores_(&scores),
      ach_(&ach),
      profile_(&profile),
      ui_(&ui),
      scenes_(&scenes),
      app_(&app) {
    // 跨服务接线也收敛在装配点：UI 的集中反馈音效需要音频实现，
    // 但 UISystem 不应自己去取（第 5 期把它的静态访问点一并移除）。
    ui_->SetAudio(*audio_);
    std::cout << "[Services] assembled (9 services, injected by composition root)" << std::endl;
}

Config&          Services::Cfg()     { return *cfg_; }
AudioSystem&     Services::Audio()   { return *audio_; }
ResourceManager& Services::Res()     { return *res_; }
ScoreRank&       Services::Scores()  { return *scores_; }
Achievements&    Services::Ach()     { return *ach_; }
PlayerProfile&   Services::Profile() { return *profile_; }
UISystem&        Services::UI()      { return *ui_; }
SceneManager&    Services::Scenes()  { return *scenes_; }
Application&     Services::App()     { return *app_; }
