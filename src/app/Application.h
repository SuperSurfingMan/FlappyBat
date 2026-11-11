#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>

class SceneManager;
class SpriteRenderer;
class Scene;
class Services;

class Application
{
public:
    // 第 5 期：不再提供 Get()。实例由 main 在栈上持有，服务引用经 Init 注入；
    // 窗口关闭回调通过 glfw 的 user pointer 取回本对象，不再依赖全局访问点。
    Application() = default;
    ~Application() = default;

    // services 的生命周期必须长于本对象（组合根里两者同为 main 的局部变量）
    bool Init(Services& services, int width, int height, const char* title);
    void Run();

    // ===== 生命周期控制 =====
    void RequestQuit();
    bool IsRunning() const;

    GLFWwindow* GetWindow() const { return window; }

    void SetSystems(SceneManager* sm, SpriteRenderer* sr);
private:
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

private:
    static void GLFWCloseCallback(GLFWwindow* window);

    // 资源就绪后构造其余场景并登记进 SceneManager 注册表（SceneKey 寻址）
    void InitScenes();
private:
    Services* services = nullptr;

    GLFWwindow* window = nullptr;

    bool running = true;

    float lastTime = 0.0f;

    // 外部系统引用（不拥有）
    SceneManager* sceneManager = nullptr;
    SpriteRenderer* spriteRenderer = nullptr;

    std::shared_ptr<Scene> bootScene = nullptr;
};
