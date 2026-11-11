#pragma once
#include <cassert>

class Config;
class AudioSystem;
class ResourceManager;
class ScoreRank;
class Achievements;
class PlayerProfile;
class UISystem;
class SceneManager;
class Application;

// =============================================================================
// 统一服务上下文（第 3 期）。
//
// 过去 8 个全局单例（Config / AudioSystem / ResourceManager / ScoreRank /
// Achievements / PlayerProfile / UISystem / SceneManager）以各自的静态
// Get()/Instance() 散落在 30+ 个文件里：依赖关系不可见、构造顺序隐式、
// 无法整体替换实现。现在由组合根在 Services::Install() 里一次性显式装配，
// 所有调用点只认这一个访问点。
//
// 这是「Service Locator 过渡形态」而非终点：下一步可把 Services 引用作为
// 构造参数注入场景/子系统，届时只需改各类的构造函数，业务调用点零改动。
// 未装配时访问会在 Debug 下断言失败，避免隐式空指针崩在别处。
// =============================================================================
class Services {
public:
    static Services& Get();
    static void Install();        // 组合根：取各服务实例并登记（幂等）
    static bool IsInstalled();

    Config&          Cfg();
    AudioSystem&     Audio();
    ResourceManager& Res();
    ScoreRank&       Scores();
    Achievements&    Ach();
    PlayerProfile&   Profile();
    UISystem&        UI();
    SceneManager&    Scenes();
    Application&     App();

private:
    Services() = default;

    template <typename T>
    static T& Require(T* p) {
        assert(p && "Services::Install() 未调用：组合根必须先装配服务上下文");
        return *p;
    }

    Config*          cfg_     = nullptr;
    AudioSystem*     audio_   = nullptr;
    ResourceManager* res_     = nullptr;
    ScoreRank*       scores_  = nullptr;
    Achievements*    ach_     = nullptr;
    PlayerProfile*   profile_ = nullptr;
    UISystem*        ui_      = nullptr;
    SceneManager*    scenes_  = nullptr;
    Application*     app_     = nullptr;
    bool             installed_ = false;
};
