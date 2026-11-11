#include "app/Services.h"
#include "app/Application.h"
#include "app/DataIntegrity.h"
#include "gameplay/ModeRegistry.h"
#include "scene/CircleFocusTransition.h"
#include "scene/SceneManager.h"
#include "render/SpriteRenderer.h"
#include "core/Config.h"
#include <iostream>
#include <memory>
#include <string>

int main()
{
    // ===== 组合根：先登记内置模式 =====
    // 注册表是模式数量、存档分段、菜单/页签遍历的唯一来源，
    // 必须早于任何单例构造（ScoreRank 构造时即按注册表大小读档）。
    RegisterBuiltinModes();

    // 组合根：显式装配统一服务上下文（所有服务实例的构造顺序集中在此）
    Services::Install();

    // 组合根：登记转场效果的构造工厂，SceneManager 不认识具体转场类型
    Services::Get().Scenes().RegisterTransitionFactory(
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

    auto& app = Services::Get().App();

    if (!app.Init(Services::Get().Cfg().GetScreenWidth(), Services::Get().Cfg().GetScreenHeight(), "Flappy Bat")) {
        system("pause");
        return -1;
    }

    // ===== 创建系统 =====
    SpriteRenderer spriteRenderer;
    SceneManager& sceneManager = Services::Get().Scenes();
    app.SetSystems(&sceneManager, &spriteRenderer);

    // ===== 运行 =====
    app.Run();

    return 0;
}