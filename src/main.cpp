#include "app/Services.h"
#include "app/Application.h"
#include "app/DataIntegrity.h"
#include "app/Achievements.h"
#include "app/PlayerProfile.h"
#include "app/ScoreRank.h"
#include "app/TuningParams.h"
#include "audio/AudioSystem.h"
#include "autoplay/AutoPilotDirector.h"
#include "core/Config.h"
#include "gameplay/ModeRegistry.h"
#include "render/SpriteRenderer.h"
#include "res/ResourceManager.h"
#include "scene/CircleFocusTransition.h"
#include "scene/SceneManager.h"
#include "ui/UISystem.h"
#include <iostream>
#include <memory>
#include <string>

// =============================================================================
// 组合根（Composition Root）
//
// 第 5 期起，这里是全程序唯一"知道所有服务"的地方：
//   1. 登记内置模式（注册表是模式数量/存档分段/菜单遍历的唯一来源，必须最早）；
//   2. 逐个取服务实例，交给 Services 聚合对象（此后所有代码只见 Services& 或更窄的依赖）；
//   3. 把 Services& 注入 Application，场景/图层/子系统再逐层拿到自己需要的那部分。
//
// 顺序说明：模式注册 -> 服务装配 -> 数据表校验 -> 窗口与资源 -> 场景构造。
// 存档类（ScoreRank / Achievements / PlayerProfile）在场景构造之前就位，
// 退出时先 ExitCurrentScene 再 Shutdown，避免静态析构期间访问已销毁的服务。
// =============================================================================
int main()
{
    // ===== 组合根：先登记内置模式 =====
    // 注册表是模式数量、存档分段、菜单/页签遍历的唯一来源，
    // 必须早于任何存档类构造（ScoreRank 构造时即按注册表大小读档）。
    RegisterBuiltinModes();

    // ===== 组合根：取服务实例并装配聚合对象 =====
    // 实例本身仍是进程级单例（生命周期与进程一致）；这里只是把"谁访问谁"显式化。
    Config&          cfg     = Config::GetInstance();
    AudioSystem&     audio   = AudioSystem::Instance();
    ResourceManager& res     = ResourceManager::Get();
    ScoreRank&       scores  = ScoreRank::Get();
    Achievements&    ach     = Achievements::Get();
    PlayerProfile&   profile = PlayerProfile::Get();
    UISystem&        ui      = UISystem::Get();

    // 调参：唯一的"设置页可改"玩法参数集（构造即读统一存档 P 行）。
    // 不做成单例——它是纯数据对象，随组合根栈上存活，比所有消费方都长命。
    TuningParams     tuning;

    // 自动演示导演：策略注册表 + 设置页开关（构造即读统一存档 U 行）。
    // 内置策略在此登记（当前仅经典模式注册策略，其它模式自动演示不生效）。
    AutoPilotDirector autoPilot;
    RegisterBuiltinPolicies(autoPilot);

    // 生命周期要点：Services 必须在 scenes / app 之后析构。
    // 声明顺序决定析构逆序——先声明持有者、后构造，保证 `~SceneManager` 里
    // 可能发生的 OnExit 仍能看到活的 Services&（退出路径已显式 ExitCurrentScene）。
    std::unique_ptr<Services> servicesHolder;
    SceneManager scenes;   // 栈上持有：比所有场景长，比服务单例短
    Application  app;

    servicesHolder = std::make_unique<Services>(
        cfg, audio, res, scores, ach, profile, tuning, autoPilot, ui, scenes, app);
    Services& services = *servicesHolder;

    // 组合根：登记转场效果的构造工厂，SceneManager 不认识具体转场类型
    scenes.RegisterTransitionFactory(
        kTransitionCircleFocus,
        []() -> std::unique_ptr<ISceneTransition> {
            return std::make_unique<CircleFocusTransition>();
        });

    // 数据表完整性前置校验：任何表项缺失都在这里以明确错误拒绝启动。
    std::string integrityError;
    if (!ValidateDataTables(&integrityError)) {
        std::cerr << "[DataIntegrity] " << integrityError << std::endl;
        return -1;
    }

    if (!app.Init(services, cfg.GetScreenWidth(), cfg.GetScreenHeight(), "Flappy Bat")) {
        system("pause");
        return -1;
    }

    // ===== 创建系统 =====
    SpriteRenderer spriteRenderer;
    app.SetSystems(&scenes, &spriteRenderer);

    // ===== 运行 =====
    app.Run();

    return 0;
}
