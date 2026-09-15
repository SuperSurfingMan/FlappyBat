#pragma once

#include "../SpriteRenderer.h"

class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;

    virtual void OnEnter() = 0;
    virtual void OnExit() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render(SpriteRenderer& renderer) = 0;
    virtual void Reset() = 0;
};