#include "SceneManager.h"
#include <iostream>
void SceneManager::ChangeScene(std::shared_ptr<Scene> nextScene) {
    std::cout << "Changing scene..." << std::endl;
    if (currentScene) {
        std::cout << "Calling OnExit..." << std::endl;
        currentScene->OnExit();
        std::cout << "Deleting current scene..." << std::endl;
    }
    std::cout << "Scene exited and deleted." << std::endl;
    currentScene = nextScene;
    if (currentScene) {
        currentScene->Reset();
        currentScene->OnEnter();
    }
    std::cout << "New scene entered." << std::endl;
}

SceneManager::SceneManager() {

}

void SceneManager::ExitCurrentScene() {
    if (currentScene) {
        currentScene->OnExit();
        currentScene = nullptr;
    }
}

SceneManager::~SceneManager() {
    if (currentScene) {
        currentScene->OnExit();
    }
}

std::shared_ptr<Scene> SceneManager::GetCurrentScene() const {
    return currentScene;
}