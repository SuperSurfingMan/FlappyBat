#include "app/Application.h"

#include "scene/SceneManager.h"
#include "scene/ReadyScene.h"
#include "scene/BootScene.h"
#include "scene/MainScene.h"
#include "scene/ScoreRankScene.h"
#include "scene/AchievementsScene.h"
#include "scene/SettingsScene.h"

#include "render/SpriteRenderer.h"
#include "core/Input.h"
#include "audio/AudioSystem.h"
#include "core/Config.h"
#include "core/Math.h"
#include "ui/FontManager.h"
#include "res/ResourceManager.h"
#include "ui/UISystem.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <iostream>

Application& Application::Get()
{
    static Application instance;
    return instance;
}

bool Application::Init(int width, int height, const char* title)
{
    if (!glfwInit()) {
        std::cerr << "GLFW INIT Failed..." << std::endl;
        return false;
    }


    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "window ptr is nullptr..." << std::endl;
        return false;
    }


    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---------------- GLAD ----------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return false;
    }

    glViewport(0, 0, width, height);

    // ---------------- ImGui ----------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    FontManager::Instance().InitFont();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---------------- OpenGL State ----------------
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Audio System
    if (!AudioSystem::Instance().Init())
    {
        std::cerr << "Failed to init AudioSystem\n";
        return false;
    }

    // 把窗口关闭事件接入引擎
    glfwSetWindowCloseCallback(window, GLFWCloseCallback);

    lastTime = (float)glfwGetTime();

    std::cout << "Resource manager initalizing... \n";

    // 仅构建加载队列（轻量）；实际资源加载延后到主循环，与 BootScene 并行执行
    ResourceManager::Get().Init();

    // 注意：其它场景（Ready/Main/ScoreRank）的构造延后到资源加载完成后，见 Run() 主循环

    std::cout << "Application initalized... \n";
    return true;
}

void Application::SetSystems(SceneManager* sm, SpriteRenderer* sr)
{
    sceneManager = sm;
    bootScene = std::make_shared<BootScene>();
    sm->Register(SceneId::Boot, bootScene);
    sm->ChangeScene(bootScene);
    spriteRenderer = sr;
}

void Application::GLFWCloseCallback(GLFWwindow* window)
{
    Application::Get().RequestQuit();
}

void Application::InitScenes() {
    // 场景构造后登记进 SceneManager 注册表；此后所有切场均以 SceneId 寻址
    sceneManager->Register(SceneId::Ready,       std::make_shared<ReadyScene>());
    sceneManager->Register(SceneId::Main,        std::make_shared<MainScene>());
    sceneManager->Register(SceneId::ScoreRank,   std::make_shared<ScoreRankScene>());
    sceneManager->Register(SceneId::Achievements,std::make_shared<AchievementsScene>());
    sceneManager->Register(SceneId::Settings,    std::make_shared<SettingsScene>());
}

void Application::RequestQuit()
{
    running = false;
}

bool Application::IsRunning() const
{
    return running;
}

void Application::Run()
{
    float projection[16];
    Ortho(
        0.0f, (float)Config::GetInstance().GetScreenWidth(),
        0.0f, (float)Config::GetInstance().GetScreenHeight(),
        projection
    );
    spriteRenderer->SetProjection(projection);
    AudioSystem::Instance().PlayBGM("assets/Audio/Music/Background Music.mp3");
    AudioSystem::Instance().SetMasterVolume(1.f);

    int frameCount = 0;
    float fpsTimer = 0.0f;
    float fps = 0.0f;

    while (running)
    {
        // 若系统请求关闭
        if (glfwWindowShouldClose(window))
            running = false;

        glfwPollEvents();

        // ===== delta time =====
        float currentTime = (float)glfwGetTime();
        float dt = currentTime - lastTime;
        dt = std::min(dt, 0.1f);
        lastTime = currentTime;

        frameCount++;
        fpsTimer += dt;
        if (fpsTimer >= 0.25f)
        {
            fps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // ===== ImGui frame =====
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("##fps_overlay", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav))
        {
            ImGui::Text("FPS: %.1f", fps);
            ImGui::End();
        }

        // ===== Render =====
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ===== 异步资源加载（与 BootScene 完全并行） =====
        // 后台线程做 IO+解码，主线程每帧只上传几张纹理 + 至多一个收尾任务，帧率不受影响
        if (!ResourceManager::Get().IsLoadingComplete()) {
            ResourceManager::Get().UpdateLoading();
            if (ResourceManager::Get().IsLoadingComplete()) {
                InitScenes(); // 资源就绪后，再构造依赖资源的其它场景
                std::cout << "All resources loaded; scenes initialized. \n";
            }
        }

        Input::Update();

        // ===== UI 事件派发：每帧一次集中命中判定，先于场景逻辑 =====
        // 控件不再自行轮询鼠标；Dispatch 之后本帧的鼠标归属与"按键是否被 UI 消费"已确定
        UISystem::Get().BeginFrame(dt);
        // 全屏交互闸门：转场进行中冻结一切控件交互（值在本帧派发前同步）
        UISystem::Get().SetInteractionBlocked(sceneManager->IsTransitionActive());
        UISystem::Get().Dispatch();

        AudioSystem::Instance().Update(); // 更新音频系统，清理已完成的音效
        sceneManager->TickTransition(dt); // 转场推进与遮蔽中点换景（先于场景逻辑，切幕当帧即新场景接管）
        sceneManager->GetCurrentScene()->Update(dt);
        sceneManager->GetCurrentScene()->Render(*spriteRenderer);
        sceneManager->DrawTransitionOverlay(); // 转场覆盖层最后追加：永远压在场景与 HUD 之上

        UISystem::Get().EndFrame();

        spriteRenderer->End();

        // ===== ImGui render =====
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ===== Cleanup（唯一出口）=====
    // 先退出当前场景：OnExit 会访问 ScoreRank/PlayerProfile/Achievements 等静态单例，
    // 必须在系统 Shutdown 之前执行；否则待静态析构阶段 SceneManager 析构再调 OnExit 时，
    // 这些后构造的单例已被销毁（逆序析构），访问已析构对象 → Segmentation Fault
    sceneManager->ExitCurrentScene();

    ResourceManager::Get().Shutdown(); // 回收后台加载线程（加载中途退出也安全）
    AudioSystem::Instance().Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}