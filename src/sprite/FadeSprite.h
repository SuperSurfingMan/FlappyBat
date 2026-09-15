#pragma once
#include "Sprite.h"

class FadeSprite : public Sprite {
public:
    FadeSprite() = default;
    ~FadeSprite() = default;

    explicit FadeSprite(std::shared_ptr<Texture2D> texture, float fadeInDuration, float fadeOutDuration, float keepDuration, Vec2 refScale, Vec2 fadeInScaleRange, Vec2 fadeOutScaleRange);

    void Update(float dt) override;

    void Reset() override;

    bool IsFinished() const;

private:
    void UpdateSpriteAlpha(float dt);

    void UpdateSpriteAlphaQuadratic(float dt);

    float CalculateQuadratic(float t);

    enum class FadeState
    {
        FadeIn,
        Keep,
        FadeOut,
        Finished
    };

    void UpdateFadeIn();
    void UpdateKeep();
    void UpdateFadeOut();
private:
    const float fadeInDuration;

    const float fadeOutDuration;

    const float keepDuration;

    const Vec2 refScale;

    const Vec2 fadeInScaleRange;
    const Vec2 fadeOutScaleRange;

    FadeState state = FadeState::FadeIn;

    float timer = 0.f;

    float spriteAlpha = 1.f;

    const float finalAlpha = 0.5f;
    const float quadraticDuration = 3;
};