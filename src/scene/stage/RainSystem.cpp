#include "scene/stage/RainSystem.h"

#include "res/ResourceManager.h"
#include "core/Config.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// =============================================================================
// 雨系统（第 5 期拆分为三个翻译单元）：
//   RainSystem.cpp          帧循环 / 雨滴推进 / 重置 / 雨强渐变
//   RainSystemLightning.cpp 预警 -> 落雷状态机 / 折线生成 / 落雷调度
//   RainSystemSplash.cpp    水花精灵池 / 地形表面采样 / 地面滚动偏移
// 三者共享同一份私有状态（类定义未拆），拆的是"变更理由"：
// 调雨滴手感不会碰到闪电调度，换地形曲线也不会碰到落雷节奏。
// =============================================================================

void RainSystem::InitStepRains(const Config& cfg) {
    size_t rainCount = cfg.GetRainSystemConfigs().first;
    float screenWidth = (float)cfg.GetScreenWidth();
    float screenHeight = (float)cfg.GetScreenHeight();
    InitRains((int)rainCount, screenWidth, screenHeight);
}

void RainSystem::InitStepSplashSets(const Config& cfg, ResourceManager& res) {
    size_t splashCount = cfg.GetRainSystemConfigs().second;
    InitSplashSets(res, splashCount);
}

void RainSystem::InitRains(int count, float w, float h)
{
    screenWidth = w;
    screenHeight = h;

    rainDrops.clear();
    rainDrops.reserve(count);

    for (int i = 0; i < count; i++)
    {
        RainDrop drop;
        drop.position.x = Random(0, screenWidth * 1.5);
        drop.position.y = Random(screenHeight, screenHeight * 1.5);
        drop.speed = Random(400.0f, 800.0f);
        drop.length = drop.speed * 0.5;
        drop.depth = Random(0.0f, 1.0f);

        rainDrops.push_back(drop);
    }

    activeDropCount = rainDrops.size(); // 初始全部激活
}

void RainSystem::Update(float dt) {
    // 闪电与雨滴是两条独立节奏：闪电在 RainSystemLightning.cpp 内自洽推进，
    // 本函数只负责"这一帧雨下到哪里"以及雨强渐变对激活数的影响。
    UpdateLightning(dt);

    // ===== 雨强渐变（A4-c） =====
    if (rainIntensity != targetRainIntensity) {
        float step = dt * 0.35f;
        rainIntensity += std::clamp(targetRainIntensity - rainIntensity, -step, step);
        size_t newCount = (size_t)std::llround((double)rainDrops.size() * (double)rainIntensity);
        newCount = std::min(newCount, rainDrops.size());
        if (newCount > activeDropCount) {
            // 新激活的雨滴重置到屏幕上方，避免画面中突然出现悬空雨滴
            for (size_t i = activeDropCount; i < newCount; ++i) {
                RespawnDrop(rainDrops[i]);
            }
        }
        activeDropCount = newCount;
    }

    UpdateRains(dt);
    UpdateTerrainAccumOffset(dt);
    UpdateSplashSprites(dt);
}

void RainSystem::Reset() {
    size_t count = rainDrops.size();

    for (size_t i = 0; i < count; ++i) {
        RainDrop& drop = rainDrops[i];
        drop.position.x = Random(0, screenWidth * 1.5);
        drop.position.y = Random(screenHeight, screenHeight * 1.5);
        drop.speed = Random(400.0f, 800.0f);
        drop.length = drop.speed * 0.5;
        drop.depth = Random(0.0f, 1.0f);
    }

    // move all splash sprites to inactive set
    auto it = rainSplashSpritesActive.begin();
    while (it != rainSplashSpritesActive.end()) {
        rainSplashSpritesInactive.insert(*it);
        it = rainSplashSpritesActive.erase(it); // erase 返回下一个有效迭代器
    }

    lightningTimer.Reset();
    isLightningActive = false;
    lightningTimeCounter = 0.f;

    // 预警/落雷状态复位（A4-a）
    lightningPhase = LightningPhase::None;
    warningTimer = 0.f;
    strikeDangerTimer = 0.f;
    deferredWarning = false;

    lightningExposure = 0.f;
    lightningFlickerTimer = 0.3f;
    terrain_accumulated_offset = 0.f;
    currentLightning = {};
}

void RainSystem::UpdateRains(float dt)
{
    // 只更新激活的雨滴（A4-c 雨强控制）
    const size_t activeCount = std::min(activeDropCount, rainDrops.size());
    for (size_t i = 0; i < activeCount; ++i)
    {
        RainDrop& drop = rainDrops[i];
        float depth = drop.depth;

        Vec2 rotation = {drop.speed, -wind};
        rotation.Normalize();

        Vec2 rain_head = {drop.position.x - drop.length * 0.5f * (-rotation.y), drop.position.y - drop.length * 0.5f * rotation.x};
        Vec2 rain_tail = {drop.position.x + drop.length * 0.5f * (-rotation.y), drop.position.y + drop.length * 0.5f * rotation.x};

        if (!drop.isFading) {
            drop.position.y -= drop.speed * dt * Lerp(0.5, 1, depth);
            drop.position.x -= wind * dt * Lerp(0.5, 1, depth);
        }
        else {
            drop.position.y -= drop.speed * dt;

            float length_on_x = drop.length * (-rotation.y);
            float target_x = (drop.hit_point.x - drop.maskHeight * length_on_x) + (drop.length * 0.5f * (-rotation.y));

            drop.position.x = target_x;
        }


        Points hit_points_near = GetEdgePoints(rain_head.x);

        bool hit_terrain_surface = rain_head.y < terrain_height && !AboveTheLine(hit_points_near.left, hit_points_near.right, rain_head);
        bool hit_terrain_bottom = rain_head.y < terrain_height - 8;
        // 出界重生
        if (!drop.isFading && (hit_terrain_surface || hit_terrain_bottom)) {
            SpawnSplash(rain_head, rotation);
            drop.hit_point = rain_head;
            drop.isFading = true;
        }

        if (drop.isFading) {
            drop.maskHeight = 1.0f - (rain_tail.y - drop.hit_point.y) / (drop.length * rotation.x) + 0.05;
            drop.hit_point.x -= 200.f * dt;
        }

        if (rain_tail.y < drop.hit_point.y || drop.position.x < 0) {
            drop.position.y = Random(screenHeight, screenHeight * 1.5);
            drop.position.x = Random(0, screenWidth * 1.5);
            drop.isFading = false;
            drop.maskHeight = 0.f;
        }
    }
}

void RainSystem::SetRainIntensity(float t) {
    targetRainIntensity = std::clamp(t, 0.05f, 1.0f);
}

void RainSystem::RespawnDrop(RainDrop& drop) {
    drop.position.x = Random(0, screenWidth * 1.5);
    drop.position.y = Random(screenHeight, screenHeight * 1.5);
    drop.speed = Random(400.0f, 800.0f);
    drop.length = drop.speed * 0.5;
    drop.depth = Random(0.0f, 1.0f);
    drop.isFading = false;
    drop.maskHeight = 0.f;
}

float RainSystem::Random(float min, float max) {
    if (min > max) return (min - max) * 0.5f + max;
    return gapRand(rng) * (max - min) + min;
}
