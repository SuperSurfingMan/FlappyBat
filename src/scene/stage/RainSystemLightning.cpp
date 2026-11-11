#include "scene/stage/RainSystem.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

// =============================================================================
// 闪电子系统（RainSystem 的闪电部分）：两条独立状态轴的推进与折线生成。
//
//   预警轴 lightningPhase —— 仅随机调度链路：None -> Warning（挂起，虚线绘制中）
//   雷体轴 isLightningActive —— 随机雷与脚本雷共用，渲染/危险判定期间为 true
//
// 不变式（与 RainSystem.h 的类内声明一致）：
//   Warning 挂起期间必无雷体（调度到点遇雷体则顺延；脚本雷会终结挂起预警），
//   雷体期间 phase 恒为 None —— 预警虚线与实际雷线因此永不同屏。
// =============================================================================

void RainSystem::UpdateLightning(float dt) {
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
    if (thunderHook_) thunderHook_();
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
