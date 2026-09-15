#include "app/Application.h"
#include "scene/SceneManager.h"
#include "SpriteRenderer.h"
#include "Config.h"

int main()
{
    auto& app = Application::Get();

    if (!app.Init(Config::GetInstance().GetScreenWidth(), Config::GetInstance().GetScreenHeight(), "Flappy Bat")) {
        system("pause");
        return -1;
    }

    // ===== 创建系统 =====
    SpriteRenderer spriteRenderer;
    SceneManager& sceneManager = SceneManager::Instance();
    app.SetSystems(&sceneManager, &spriteRenderer);

    // ===== 运行 =====
    app.Run();

    return 0;
}