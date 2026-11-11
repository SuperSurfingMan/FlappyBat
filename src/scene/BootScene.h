#pragma once
#include "scene/Scene.h"
#include <memory>

class Sprite;
class Services;

class BootScene : public Scene{
public:
    explicit BootScene(Services& svc_);
    ~BootScene() = default;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;

    bool IsFinished() const;

    void Reset() override;
private:
    Services& svc_; // 构造期注入的服务聚合

    std::shared_ptr<Sprite> background;
    std::shared_ptr<Sprite> text;

};