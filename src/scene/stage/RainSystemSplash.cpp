#include "scene/stage/RainSystem.h"

#include "res/ResourceManager.h"

#include <algorithm>

// =============================================================================
// 水花与地形（RainSystem 的表现侧数据）：
//   - 水花精灵池：预分配若干 Sprite，命中地面时取一个播放一次消散动画后归还；
//   - 地形表面采样：给定 x 返回该处地面折线的左右端点（雨滴命中判定用）；
//   - 地面滚动偏移：让水花随地形一起左移并在 270px 周期内回绕。
//
// 这里的地形曲线是硬编码的像素表（与 ground 贴图同源）。若要换地形美术，
// 改动集中在本文件，雨滴推进与闪电调度完全不受影响。
// =============================================================================

void RainSystem::InitSplashSets(ResourceManager& res, size_t set_size) {
    auto rainSplashSpritesheet = res.GetTexture2D("rainSplash");
    rainSplashSpritesInactive.reserve(set_size);
    rainSplashSpritesActive.reserve(set_size);
    for (size_t i = 0;i < set_size; ++i) {
        auto splashSprite = std::make_shared<Sprite>();
        splashSprite->InitAnimationByTexture(rainSplashSpritesheet, 5);
        splashSprite->GetAnimation()->SetAnimationProperties(false, true);
        splashSprite->SetScale(32, 64);
        rainSplashSpritesInactive.insert(splashSprite);
    }
}

void RainSystem::UpdateSplashSprites(float dt) {
    auto it = rainSplashSpritesActive.begin();
    while (it != rainSplashSpritesActive.end()) {
        auto splashSprite = *it;
        splashSprite->Update(dt);
        Vec2 position = splashSprite->GetPosition();
        splashSprite->SetPosition(position.x - dt * scrollSpeed, position.y);
        const auto& anim = splashSprite->GetAnimation();
        if (anim->IsFinished()) {
            anim->Reset();
            it = rainSplashSpritesActive.erase(it);  // erase 返回下一个有效迭代器
            rainSplashSpritesInactive.insert(splashSprite);
        } else {
            ++it;
        }
    }
}

void RainSystem::SpawnSplash(Vec2 position, Vec2 rotation) {
    if (!rainSplashSpritesInactive.empty()) {
        auto it = rainSplashSpritesInactive.begin();
        (*it)->SetPosition(position.x, position.y);
        (*it)->SetRotation(rotation);
        rainSplashSpritesActive.insert(*it);
        rainSplashSpritesInactive.erase(it);
    }
}

void RainSystem::UpdateTerrainAccumOffset(float dt) {
    terrain_accumulated_offset += dt * scrollSpeed;
    if (terrain_accumulated_offset >= 270.0f)
        terrain_accumulated_offset -= 270.0f;
}

Points RainSystem::GetEdgePoints(float x) {
    if (x < 0.f || x > screenWidth) return Points{Vec2::Zero(), Vec2::Zero()};
    x = (int)(x + terrain_accumulated_offset) % (int)screenWidth;
    if (x < screenWidth * 0.5f)
        return GetEdgePointsHelper(x);
    else {
        Vec2 offset = {screenWidth * 0.5f, 0};
        return GetEdgePointsHelper(x) + Points{offset, offset};
    }
}

Points RainSystem::GetEdgePointsHelper(float x) {
    if (x > 254.0f) {
        if (x < 262.0f) return Points{Vec2{254.0f, 97.0f}, Vec2{262.f, 104.f}};
        else return Points{Vec2{262.f, 104.f}, Vec2{269.f, 97.f}};
    }
    else if (x > 223.0f) {
        Vec2 left = {224, 97};
        Vec2 mid = {232, 104};
        Vec2 right = {238, 98};
        Vec2 x_offset = {15, 0};
        if (x < 232.0f) return Points{left, mid};
        if (x < 239.0f) return Points{mid, right};
        if (x < 247.0f) return Points{left + x_offset, mid + x_offset};
        return {mid + x_offset, right + x_offset};
    }
    else {
        int seq_num = (int)(x / 16); // begin from 0
        Vec2 x_offset = {(float)(16 * seq_num), 0};
        Vec2 left = Vec2{0, 97} + x_offset;
        Vec2 mid = Vec2{8, 104} + x_offset;
        Vec2 right = Vec2{15, 97} + x_offset;
        if (x < mid.x) {
            return Points{left, mid};
        }
        else {
            return Points{mid, right};
        }
    }
}
