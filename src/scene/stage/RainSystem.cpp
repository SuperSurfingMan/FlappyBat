#include "scene/stage/RainSystem.h"

#include "res/ResourceManager.h"
#include "audio/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

void RainSystem::InitStepRains() {
    size_t rainCount = Config::GetInstance().GetRainSystemConfigs().first;
    float screenWidth = (float)Config::GetInstance().GetScreenWidth();
    float screenHeight = (float)Config::GetInstance().GetScreenHeight();
    InitRains((int)rainCount, screenWidth, screenHeight);
}

void RainSystem::InitStepSplashSets() {
    size_t splashCount = Config::GetInstance().GetRainSystemConfigs().second;
    InitSplashSets(splashCount);
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
    if (lightningTimer.IsRunning()) {
        lightningTimer.Update(dt);
    }

    if (lightningTimer.IsFinished()) {
        lightningTimer.Reset();
        float interval = Random(intervalMin, intervalMax);
        // 定时器到点：先进入预警阶段（A4-a），预警结束才真正落雷
        lightningTimer.Start(interval, [this](){
            EnterLightningWarning();
        });
    }

    // ===== 预警 -> 落雷 状态机（A4-a）：预警轴只负责挂起与倒计时，雷体轴见下方 isLightningActive 块 =====
    if (lightningPhase == LightningPhase::Warning) {
        warningTimer -= dt;
        if (warningTimer <= 0.0f) {
            BeginLightningStrike();
        }
    }

    // 控制雷电间断闪烁
    if (isLightningActive)
    {
        strikeDangerTimer = std::max(0.0f, strikeDangerTimer - dt); // 危险窗跟随雷体轴计时，与预警相位无关
        lightningFlickerTimer -= dt;

        if (lightningFlickerTimer < 0)
        {
            isLightning = !isLightning;
            lightningFlickerTimer = isLightning ? Random(0.2f, 0.3f) : Random(0.05f, 0.1f);
            if (isLightning) lightningExposure = Random(0.6f, 1.0f);
            else lightningExposure = 0.f;
        }
    }

    // 控制雷电总时长
    if (isLightningActive) {
        lightningTimeCounter += dt;
        // 结束时的动作
        if (lightningTimeCounter >= lightningLastTime) {
            isLightningActive = false;
            isLightning = false;
            currentLightning = {};
            lightningTimeCounter = 0.f;
            lightningExposure = 0.f;
            strikeDangerTimer = 0.f;
            // 雷体结束，威胁解除（phase 在 BeginLightningStrike 已归 None）；
            // 调度到点时被顺延的预警此刻进入，保证预警虚线不与雷体同屏
            if (deferredWarning) {
                deferredWarning = false;
                EnterLightningWarning();
            }
        }
    }

    if (isLightning) {
        lightningExposure -= dt * 5.0f;
        lightningExposure = std::max(lightningExposure, 0.0f);
    }

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

void RainSystem::GenerateLightning(float strikeX)
{
    currentLightning = {};

    // 自落点向上生成主分支（锚定预警位置）。
    // 玩法状态参数收紧：每段抖动 ±35、总漂移相对落点钳制 ±120，保证折线走廊可读、横向逃脱距离可控
    float x = strikeX;
    float y = terrain_height;

    LightningBranch main;
    main.thickness = 20.0f;

    main.points.push_back({x, y});

    while (y < screenHeight) // 逐段爬升至天顶
    {
        x += Random(-35.f, 35.f);
        x = std::clamp(x, strikeX - StrikeDriftClamp, strikeX + StrikeDriftClamp); // 总漂移钳制
        y += Random(200.f, 350.f);

        main.points.push_back({x, y});
    }

    currentLightning.mainBranch = main;

    // 生成分叉（纯装饰，不参与判定）
    GenerateBranches(currentLightning.mainBranch, 1);
}

void RainSystem::EnterLightningWarning() {
    // 雷体渲染中（脚本雷未结束）：本次预警顺延至雷体结束后立刻进入，
    // 避免实际雷线与预警虚线/包围盒同屏；调度链的下一次到点不受影响
    if (isLightningActive) {
        deferredWarning = true;
        return;
    }
    // 已有预警挂起时忽略本次到点（间隔下限校验下不会发生，防御未来调参破坏不变式）
    if (lightningPhase == LightningPhase::Warning) {
        return;
    }
    lightningPhase = LightningPhase::Warning;
    strikeX = Random(100.0f, screenWidth - 100.0f);
    warningTimer = lightningWarnDuration;
    // 预生成雷折线：预警显示、落雷渲染、命中判定三方共用同一路径（所见即所判）
    GenerateLightning(strikeX);
}

void RainSystem::BeginLightningStrike() {
    // 挂起中的随机预警随落雷终结（脚本雷可抢占预警期，取代该次随机雷）；
    // 预警管线无需重排——调度链在预警开始时已排定下一次到点
    lightningPhase = LightningPhase::None;
    warningTimer = 0.0f;
    isLightningActive = true;
    isLightning = true; // 落雷瞬间立即可见（渲染门控读的是 isLightning，不能等 0.3s 后的首次闪烁翻转）
    lightningExposure = 1.0f;
    lightningFlickerTimer = 0.3f;
    lightningTimeCounter = 0.f;
    strikeDangerTimer = LightningDangerDuration;
    // 雷击音效与闪电视觉同步触发（短而强）；雷路径已在预警开始时生成，此处不再重建
    std::cout << "[Audio] Thunder strike at x=" << strikeX << "\n";
    AudioSystem::Instance().PlaySFX(SFXToFile(SFX::Thunder), 1.0f);
}

float RainSystem::GetTimeToNextLightning() const {
    // 预警挂起中、雷体渲染中或定时器未运转时无意义（雷体期间不倒计时，保持消息盒安静）
    if (lightningPhase != LightningPhase::None || isLightningActive || !lightningTimer.IsRunning()) return -1.0f;
    return lightningTimer.GetRemaining();
}

float RainSystem::GetMainBranchWidthAt(size_t pointIndex) const {
    const auto& pts = currentLightning.mainBranch.points;
    if (pts.size() < 2) return 0.0f;
    // 与 LightningRenderer::BuildBranchMesh 完全一致的锥形收细公式
    float t = (float)pointIndex / (float)(pts.size() - 1);
    return currentLightning.mainBranch.thickness * (1.8f * (1.0f - t * t) + 0.3f);
}

void RainSystem::SetLightningIntervals(float minSec, float maxSec) {
    if (minSec > maxSec) std::swap(minSec, maxSec);
    // 保证间隔大于「预警 + 落雷持续」并留缓冲，避免两次闪电视觉重叠
    float floorSec = lightningWarnDuration + lightningLastTime + 0.5f;
    intervalMin = std::max(minSec, floorSec);
    intervalMax = std::max(maxSec, intervalMin);
}

void RainSystem::TriggerStrikeAt(float strikeX) {
    // 脚本雷立即落雷（不走预警相）；挂起中的随机预警由 BeginLightningStrike 统一终结
    strikeX = std::clamp(strikeX, 100.0f, screenWidth - 100.0f);
    GenerateLightning(strikeX);
    BeginLightningStrike();
}

void RainSystem::SetLightningWarnLead(float sec) {
    // 预警不得短于危险窗口（否则失去提前量意义），上限防拖沓
    lightningWarnDuration = std::clamp(sec, LightningDangerDuration, 3.0f);
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

void RainSystem::GenerateBranches(const LightningBranch& parent, int depth)
{
    if (depth > 2) return;

    for (size_t i = 1; i < parent.points.size() - 1; ++i)
    {
        if (Random(0.f, 1.f) < 0.35f)
        {
            LightningBranch branch;
            branch.thickness = parent.thickness * 0.5f;

            Vec2 start = parent.points[i];
            branch.points.push_back(start);

            Vec2 dir = parent.points[i] - parent.points[i - 1];
            dir.Normalize();

            float length = Random(200.f, 400.f);
            int segments = 3 + rand() % 3;

            Vec2 current = start;

            for (int s = 0; s < segments; ++s)
            {
                Vec2 offset = Vec2(
                    Random(-80.f, 80.f),
                    -Random(80.f, 150.f)
                );

                current += offset;
                branch.points.push_back(current);
            }

            currentLightning.subBranches.push_back(branch);

            GenerateBranches(branch, depth + 1);
        }
    }
}

void RainSystem::StartRandomLightning(float interval_min, float interval_max) {
    SetLightningIntervals(interval_min, interval_max); // 统一走「预警+雷体+缓冲」下限校验，保证调度间隔不小于单次周期
    float interval = Random(intervalMin, intervalMax);
    // 首次及后续闪电统一走「预警 -> 落雷」流程（A4-a）
    lightningTimer.Start(interval, [this](){
        EnterLightningWarning();
    });
}

void RainSystem::InitSplashSets(size_t set_size) {
    auto rainSplashSpritesheet = ResourceManager::Get().GetTexture2D("rainSplash");
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

float RainSystem::Random(float min, float max) {
    if (min > max) return (min - max) * 0.5f + max;
    return gapRand(rng) * (max - min) + min;
}
