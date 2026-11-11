#include "app/Services.h"
#include "scene/SceneManager.h"
#include "core/Config.h"
#include <iostream>

void SceneManager::RegisterTransitionFactory(TransitionKey style,
                                            TransitionFactory factory) {
    transitionFactories[style] = std::move(factory);
}

std::unique_ptr<ISceneTransition> SceneManager::MakeTransition(TransitionKey style) {
    auto it = transitionFactories.find(style);
    if (it == transitionFactories.end() || !it->second) return nullptr;
    return it->second();
}

SceneManager::SceneManager() {

}

SceneManager::~SceneManager() {
    if (currentScene) {
        currentScene->OnExit();
    }
}

// ===== 场景注册表 =====

void SceneManager::Register(SceneKey id, std::shared_ptr<Scene> scene) {
    registry[id] = std::move(scene);
}

std::shared_ptr<Scene> SceneManager::Resolve(SceneKey id) const {
    auto it = registry.find(id);
    return it != registry.end() ? it->second : nullptr;
}

// ===== 声明式切场 =====

void SceneManager::RequestScene(SceneKey id, const SceneArgs& args, TransitionKey style) {
    if (transition) return; // 转场进行中不接受新请求（防重入）
    auto next = Resolve(id);
    if (!next) {
        // 未注册的键一律拒绝并指名：静默忽略会让「新增场景忘了登记」变成
        // 点按钮没反应这种无从排查的现象
        std::cerr << "RequestScene: 场景键未注册 -> \"" << id << "\"（检查组合根 Register）" << std::endl;
        return;
    }

    if (style == kTransitionNone) { // 无样式：维持同帧直切语义
        ChangeScene(std::move(next), args);
        return;
    }

    // 启动转场：换景延后到遮蔽中点（TickTransition）
    pendingScene = std::move(next);
    pendingArgs = args;
    transition = MakeTransition(style);
    if (transition) {
        TransitionContext ctx;
        const float w = (float)Services::Get().Cfg().GetScreenWidth();
        const float h = (float)Services::Get().Cfg().GetScreenHeight();
        ctx.focusAnchor = args.focusAnchorScreen.value_or(Vec2{ w * 0.5f, h * 0.5f });
        transition->Start(ctx);
    } else {
        pendingScene = nullptr; // 未知样式：按防御处理，不切场
        std::cerr << "RequestScene: 转场样式未注册 -> \"" << style << "\"（检查组合根工厂注册）" << std::endl;
    }
}

// ===== 转场驱动 =====

void SceneManager::TickTransition(float dt) {
    if (!transition) return;
    transition->Update(dt);

    if (pendingScene && transition->ReadyToSwitch()) {
        // 遮蔽到位：换景（转场对象与续态保持不动，由本管理器继续持有）
        auto next = std::move(pendingScene);
        SceneArgs args = pendingArgs;
        pendingScene = nullptr;
        pendingArgs = SceneArgs{};
        SwapScene(std::move(next), args);
    }

    if (transition && transition->IsFinished()) {
        transition.reset(); // 揭示完毕，拆除覆盖层
    }
}

void SceneManager::DrawTransitionOverlay() {
    if (transition) transition->Draw();
}

bool SceneManager::IsTransitionActive() const {
    return transition != nullptr && transition->BlocksInteraction();
}

// ===== 换景与生命周期 =====

void SceneManager::ChangeScene(std::shared_ptr<Scene> nextScene, const SceneArgs& args) {
    // 同步直切：若恰有转场在播（当前仅调试路径可能发生），一并拆除避免悬空覆盖层
    transition.reset();
    pendingScene = nullptr;
    SwapScene(std::move(nextScene), args);
}

void SceneManager::SwapScene(std::shared_ptr<Scene> nextScene, const SceneArgs& args) {
    std::cout << "Changing scene..." << std::endl;
    if (currentScene) {
        std::cout << "Calling OnExit..." << std::endl;
        currentScene->OnExit();
        std::cout << "Scene exited." << std::endl;
    }
    currentScene = std::move(nextScene);
    if (currentScene) {
        currentScene->OnPrepare(args); // 启动参数先于 Reset 注入（如对局模式决定参数表）
        currentScene->Reset();
        currentScene->OnEnter();
    }
    std::cout << "New scene entered." << std::endl;
}

void SceneManager::ExitCurrentScene() {
    if (currentScene) {
        currentScene->OnExit();
        currentScene = nullptr;
    }
}

std::shared_ptr<Scene> SceneManager::GetCurrentScene() const {
    return currentScene;
}
