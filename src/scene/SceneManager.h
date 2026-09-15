#pragma once
#include "Scene.h"
#include <memory>

class SceneManager {
public:
    static SceneManager& Instance() {
        static SceneManager instance; // 初始场景为 nullptr，后续通过 ChangeScene 设置
        return instance;
    }

    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    void ChangeScene(std::shared_ptr<Scene> nextScene);
    std::shared_ptr<Scene> GetCurrentScene() const;
    // 退出当前场景（OnExit + 置空）：由 Application 在系统 Shutdown 前调用，
    // 确保 OnExit 执行时各单例（ScoreRank/PlayerProfile/Achievements 等）尚未析构
    void ExitCurrentScene();

private:
    SceneManager();  
    std::shared_ptr<Scene> currentScene;
};