#include "scene/stage/ParallaxField.h"

#include "res/ResourceManager.h"
#include "sprite/Sprite.h"

void ParallaxField::Load(ResourceManager& res) {
    background_ = res.GetSpriteList("background");
    foreground_ = res.GetSpriteList("foreground");
    terrain_ = res.GetSpriteList("terrain");
}

void ParallaxField::LayoutStrip(std::vector<std::shared_ptr<Sprite>>& strip, float x0, float y) {
    for (size_t i = 0; i < strip.size(); ++i) {
        strip[i]->SetPosition(x0 + (float)i * kTileWidth, y);
    }
}

void ParallaxField::ScrollStrip(std::vector<std::shared_ptr<Sprite>>& strip, float dx) {
    // 回绕距离由块数推导：原来是硬编码的 270.0f * 3（只对三块贴图成立）
    const float wrap = WrapDistance(strip.size());
    if (wrap <= 0.0f) return;

    for (auto& s : strip) {
        auto pos = s->GetPosition();
        pos.x -= dx;
        if (pos.x <= -kTileWidth * 0.5f) pos.x += wrap;
        s->SetPosition(pos.x, pos.y);
    }
}

void ParallaxField::Reset(float screenW, float screenH, float terrainHeight, float terrainRenderHeight) {
    (void)screenW; // 三段条带宽度由块数与块宽决定，与窗口宽度无关（保持原布局语义）
    const float skyHeight = screenH - terrainHeight; // 天空区（地面以上）高度

    LayoutStrip(background_, kTileWidth * 0.5f, skyHeight * 0.5f + terrainHeight);
    LayoutStrip(foreground_, kTileWidth * 0.5f, skyHeight * 0.5f + terrainHeight);
    LayoutStrip(terrain_,    kTileWidth * 0.5f, terrainRenderHeight * 0.5f);
}

void ParallaxField::Scroll(float dt, float backgroundSpeed, float foregroundSpeed, float terrainSpeed) {
    ScrollStrip(background_, backgroundSpeed * dt);
    ScrollStrip(foreground_, foregroundSpeed * dt);
    ScrollStrip(terrain_,    terrainSpeed * dt);
}
