#pragma once
#include "../SpriteRenderer.h"

class Layer {
public:
    Layer() = default;
    virtual ~Layer() = default;

    virtual void Update(float dt) = 0;
    virtual void Render(SpriteRenderer& renderer) = 0;
    virtual void Reset() = 0;
};

