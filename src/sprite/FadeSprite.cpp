#include "FadeSprite.h"
#include "../Math.h"

FadeSprite::FadeSprite(std::shared_ptr<Texture2D> texture, float fadeInDuration, float fadeOutDuration, float keepDuration, 
    Vec2 refScale, Vec2 fadeInScaleRange, Vec2 fadeOutScaleRange) : 
    Sprite(texture), fadeInDuration(fadeInDuration),
    fadeOutDuration(fadeOutDuration), keepDuration(keepDuration), refScale(refScale), 
    fadeInScaleRange(fadeInScaleRange), fadeOutScaleRange(fadeOutScaleRange)
{}

void FadeSprite::Update(float dt) {
    Sprite::Update(dt);
    if (IsFinished()) return;
    // UpdateSpriteAlpha(dt);
    UpdateSpriteAlphaQuadratic(dt);
}

void FadeSprite::Reset() {
    Sprite::Reset();
    state = FadeState::FadeIn;
    timer = 0.f;
    scale = refScale;
    color[3] = 1.f;
    spriteAlpha = 1.f;
}

bool FadeSprite::IsFinished() const {
    return state == FadeState::Finished;
}

void FadeSprite::UpdateSpriteAlpha(float dt)
{
    timer += dt;

    switch (state)
    {
        case FadeState::FadeIn:
            UpdateFadeIn();
            break;

        case FadeState::Keep:
            UpdateKeep();
            break;

        case FadeState::FadeOut:
            UpdateFadeOut();
            break;

        case FadeState::Finished:
            return;
    }
}

void FadeSprite::UpdateFadeIn() {
    float t = Clamp(0.f, 1.f, timer / fadeInDuration);

    spriteAlpha = t;

    float scaleFactor = Lerp(fadeInScaleRange.x, fadeInScaleRange.y, t);

    scale = refScale * scaleFactor;

    color[3] = spriteAlpha;

    if (timer >= fadeInDuration)
    {
        state = FadeState::Keep;
        timer = 0.f;
    }
}

void FadeSprite::UpdateKeep() {
    scale = refScale;
    color[3] = 1.0f;

    if (timer >= keepDuration)
    {
        state = FadeState::FadeOut;
        timer = 0.f;
    }
}

void FadeSprite::UpdateFadeOut() {
    float t = Clamp(0.f, 1.f, timer / fadeOutDuration);

    spriteAlpha = Lerp(1.0f, finalAlpha, t);

    float scaleFactor = Lerp(fadeOutScaleRange.x, fadeOutScaleRange.y, t);

    scale = refScale * scaleFactor;

    color[3] = spriteAlpha;

    if (timer >= fadeOutDuration)
    {
        state = FadeState::Finished;
    }
}

void FadeSprite::UpdateSpriteAlphaQuadratic(float dt) {
    timer += dt;
    if (timer > quadraticDuration) {
        state = FadeState::Finished;
        return;
    }
    spriteAlpha = CalculateQuadratic(timer);
    color[3] = spriteAlpha;
}

float FadeSprite::CalculateQuadratic(float t) {
    float x = Clamp(0.f, 1.f, t / quadraticDuration);
    scale = refScale * Lerp(0.8f, 1.05f, x);
    float alpha = 0.f;
    if (x < 0.5f) {
        alpha = 4 * (x * x);
    }
    else {
        float a = 0.2f, b = -0.9f;
        alpha = a * (x * x) + b * x + 1.4f;
    }

    return Clamp(0.f, 1.f, alpha);
}