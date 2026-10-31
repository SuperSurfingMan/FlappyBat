#pragma once
#include "scene/Scene.h"
#include <memory>

class Sprite;

class BootScene : public Scene{
public:
    BootScene();
    ~BootScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;

    bool IsFinished() const;

    void Reset() override;
private:
    std::shared_ptr<Sprite> background;
    std::shared_ptr<Sprite> text;

};