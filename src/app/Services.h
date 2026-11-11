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
// 服务聚合（第 5 期：静态访问点退场）
//
// 第 3 期引入的 Services 是"Service Locator 过渡形态"——调用点统一了，
// 但访问点仍是全局静态函数：谁在用谁、构造顺序如何，只能靠读实现。
//
// 现在本类是一个**普通对象**：由组合根（main）在栈上构造，再把引用显式下发。
//   main -> Application::Init(Services&)
//        -> 场景构造参数（ReadyScene / MainScene / ...）
//        -> 图层构造参数（MainSceneUILayer / MainSceneOverLayer）
//        -> 子系统的 Init/构造参数（WeatherDirector / PlayerAbilities / ...）
//
// 服务实例本身仍是进程级单例（Config / AudioSystem / ... 的生命周期与进程一致，
// 且存档类必须在任何场景之前就位），但"访问路径"已从全局函数改为对象引用：
// 依赖关系出现在构造函数签名里，测试可以整组替换，也不会再出现
// "某个类在静态初始化期偷偷构造了一个服务"的隐式顺序。
//
// 只装配不拥有：本类持有裸引用，不负责析构任何服务。
// =============================================================================
class Services {
public:
    Services(Config& cfg,
             AudioSystem& audio,
             ResourceManager& res,
             ScoreRank& scores,
             Achievements& ach,
             PlayerProfile& profile,
             UISystem& ui,
             SceneManager& scenes,
             Application& app);

    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

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
    Config*          cfg_;
    AudioSystem*     audio_;
    ResourceManager* res_;
    ScoreRank*       scores_;
    Achievements*    ach_;
    PlayerProfile*   profile_;
    UISystem*        ui_;
    SceneManager*    scenes_;
    Application*     app_;
};
