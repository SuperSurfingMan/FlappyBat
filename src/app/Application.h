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
    static Application& Get();

    bool Init(int width, int height, const char* title);
    void Run();

    // ===== 生命周期控制 =====
    void RequestQuit();
    bool IsRunning() const;

    GLFWwindow* GetWindow() const { return window; }

    void SetSystems(SceneManager* sm, SpriteRenderer* sr);
private:
    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

private:
    static void GLFWCloseCallback(GLFWwindow* window);

    // 资源就绪后构造其余场景并登记进 SceneManager 注册表（SceneKey 寻址）
    void InitScenes();
private:
    GLFWwindow* window = nullptr;

    bool running = true;

    float lastTime = 0.0f;

    // 外部系统引用（不拥有）
    SceneManager* sceneManager = nullptr;
    SpriteRenderer* spriteRenderer = nullptr;

    std::shared_ptr<Scene> bootScene = nullptr;
};
