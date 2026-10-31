#include "sprite/ColorfulSprite.h"
#include "core/Math.h"

ColorfulSprite::ColorfulSprite(std::shared_ptr<Texture2D> baseTexture, float periodTime) : period(periodTime) {
    texture = baseTexture;

}

void ColorfulSprite::Update(float dt) {
    timer += dt;
    timer = (timer >= period) ? 0.f : timer;
}

void ColorfulSprite::Reset() {
    Sprite::Reset();
    timer = 0.f;
}

float ColorfulSprite::GetProgress() const {
    return Clamp(0.f, 1.f, timer / period);
}
