#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>

class SceneManager;
class SpriteRenderer;
class Scene;

class Application
{
public:
    enum class Scenes {
        ReadyScene,
        MainScene,
        ScoreRankScene,
        AchievementsScene,
        SettingsScene
    };

    static Application& Get();

    bool Init(int width, int height, const char* title);
    void Run();

    // ===== 生命周期控制 =====
    void RequestQuit();
    bool IsRunning() const;

    GLFWwindow* GetWindow() const { return window; }

    void SetSystems(SceneManager* sm, SpriteRenderer* sr);

    std::shared_ptr<Scene> GetScene(Scenes scene);
private:
    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

private:
    static void GLFWCloseCallback(GLFWwindow* window);

    void InitScenes();
private:
    GLFWwindow* window = nullptr;

    bool running = true;

    float lastTime = 0.0f;

    // 外部系统引用（不拥有）
    SceneManager* sceneManager = nullptr;
    SpriteRenderer* spriteRenderer = nullptr;

    std::shared_ptr<Scene> bootScene = nullptr;
    std::shared_ptr<Scene> readyScene = nullptr;
    std::shared_ptr<Scene> mainScene = nullptr;
    std::shared_ptr<Scene> scoreRankScene = nullptr;
    std::shared_ptr<Scene> achievementsScene = nullptr;
    std::shared_ptr<Scene> settingsScene = nullptr;
};