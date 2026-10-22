#include "ObstacleField.h"
#include "../../Config.h"
#include "../../SpriteRenderer.h"
#include "../../res/ResourceManager.h"
#include "../../sprite/Sprite.h"
#include "../../sprite/BatSprite.h"
#include "../../audio/AudioSystem.h"
#include "../../enemy/IStageFeedback.h"
#include "../../ui/UITheme.h"
#include <algorithm>
#include <cmath>
#include <iostream>

void ObstacleField::Load() {
    // 初始化 UV 坐标，用于碰撞检测
    upCollisionUV = ResourceManager::Get().GetUVcords3("obstacleUp");
    downCollisionUV = ResourceManager::Get().GetUVcords3("obstacleDown");
    batCollisionUV = ResourceManager::Get().GetUVcords2("bat");

    obstaclesUpPool = ResourceManager::Get().GetSpriteList("obstaclesUpPool");
    obstaclesDownPool = ResourceManager::Get().GetSpriteList("obstaclesDownPool");
}

void ObstacleField::Reset(const StageFrame& f) {
    tier_ = f.tier;
    gapSize_ = f.gapSize;
    screenW_ = f.screenW;
    screenH_ = f.screenH;
    terrainY_ = f.terrainY;

    runSmashCount = 0;
    runNearMissCount = 0;
    oscillTime = 0.0f;
    for (auto& fly : flyingStates) fly = FlyingState{};

    for (size_t i = 0; i < obstaclesUpPool.size(); ++i) {
        obstaclesUpPool[i]->SetRotation(Vec2(1.0f, 0.0f));
        obstaclesDownPool[i]->SetRotation(Vec2(1.0f, 0.0f));
        obstaclesUpPool[i]->SetAlpha(1.0f);
        obstaclesDownPool[i]->SetAlpha(1.0f);
    }

    const float GAP_SIZE = gapSize_; // 难度递进：初始为第 0 档间隙
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();

    float obs_start_x = screenW_ + Config::GetInstance().GetObstacleWidth() / 2; // 从屏幕右边开始生成
    float gap_center = (screenH_ - terrainY_) * 0.5f + terrainY_; // 初始 gap 中心固定为正中心

    float prevColumnX = -1.0f; // 上一列障碍物中心（用于在列间走廊生成道具）
    for (size_t i = 0; i < obstaclesUpPool.size() && i < obstacleStates.size(); ++i) {
        auto& st = obstacleStates[i];
        st.type = RollObstacleVariant();
        st.gapSize = (st.type == 2) ? NarrowGapOf(GAP_SIZE) : GAP_SIZE;

        // 按该障碍物自身的间隙限制中心位置
        float max_gap_center = screenH_ - SAFE_MARGIN - st.gapSize * 0.5f;
        float min_gap_center = SAFE_MARGIN + st.gapSize * 0.5f + terrainY_;
        gap_center = std::clamp(gap_center, min_gap_center, max_gap_center);

        st.baseGapCenter = gap_center;
        st.oscillAmp = 0.0f;
        st.oscillPhase = 0.0f;
        st.nearMissChecked = false;
        ApplyObstacleGeometry(i, obs_start_x, gap_center, st.gapSize);
        SetObstacleTint(i, st.type);

        // 障碍间走廊随机生成道具（跳过第一列之前的区域）
        if (prevColumnX > 0.0f && spawnCorridorItem_) {
            spawnCorridorItem_(prevColumnX, obs_start_x);
        }
        prevColumnX = obs_start_x;

        float spacing = Config::GetInstance().GetSpacingRange().first +
            Random::Get().GetRandomFloat() *
            (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
        obs_start_x += spacing;
        float delta = Config::GetInstance().GetGapCenterDeltaRange().first +
            Random::Get().GetRandomFloat() *
            (Config::GetInstance().GetGapCenterDeltaRange().second - Config::GetInstance().GetGapCenterDeltaRange().first);
        gap_center += delta;
    }
    lastSpawnedGapCenter = gap_center; // 供飞行回收重插入时衔接
}

void ObstacleField::Update(float dt, const StageFrame& f) {
    tier_ = f.tier;
    gapSize_ = f.gapSize;
    screenW_ = f.screenW;
    screenH_ = f.screenH;
    terrainY_ = f.terrainY;
    freezeOffscreenPipes_ = f.freezeOffscreenPipes;
    suppressPipeSpawn_ = f.suppressPipeSpawn;

    ScrollAndScore(dt, f);
    RecycleColumns(f);
}

void ObstacleField::ScrollAndScore(float dt, const StageFrame& f) {
    const float HALF_WIDTH = Config::GetInstance().GetObstacleWidth() * 0.5f;
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();

    const float move = f.obstacleSpeed * dt;
    oscillTime += dt; // 摆动变体全局时钟

    // ===== 1. 向左移动 =====
    for (size_t i = 0; i < obstaclesUpPool.size() && i < obstacleStates.size(); ++i) {
        const bool upFlying = i < flyingStates.size() && flyingStates[i].up.flying;
        const bool downFlying = i < flyingStates.size() && flyingStates[i].down.flying;
        if (upFlying && downFlying) continue; // 整列飞行：完全交给 UpdateFlying

        auto up = obstaclesUpPool[i];
        auto down = obstaclesDownPool[i];
        auto& st = obstacleStates[i];

        // 列锚点：整列完好取上管；单管被撞飞时取在列的那根
        float colX = upFlying ? down->GetPosition().x : up->GetPosition().x;

        // 段落期（怪物接管）：尚未入画的列冻结在屏外，不再从右缘滚入；
        // 已入画（左缘越过屏幕右缘）的残管照常滚动自然离场
        if (freezeOffscreenPipes_ && colX - HALF_WIDTH >= screenW_) continue;

        float newX = colX - move;
        const bool pairWhole = !upFlying && !downFlying; // 残缺列不计分/贴脸/摆动

        // 计分：越过屏幕中心线 +1（锚点固定，不受蝙蝠横移影响）
        if (pairWhole && up->GetPosition().x >= screenW_ * 0.5f && newX < screenW_ * 0.5f) {
            if (fx_) {
                fx_->AddScore(1); // 统一入口：含模式倍率与成就上报
                fx_->ReportAchievement(AchievementEvent::PipePassed, 1);
                fx_->PlaySFX((int)SFX::Point, 0.9f);
            }
        }

        // ===== 贴脸判定：以蝙蝠当前 x 为锚点（蝙蝠可横移，不再恒在屏幕中心） =====
        // 每对障碍物只判定一次，防止蝙蝠左右往返造成"反复越过"重复触发
        const float batX = f.batPos.x;
        if (pairWhole && !st.nearMissChecked && up->GetPosition().x >= batX && newX < batX) {
            st.nearMissChecked = true;
            const Vec2& batPos = f.batPos;
            const Vec2& batScale = f.batScale;
            float gapLowerEdge = down->GetPosition().y + down->GetScale().y * 0.5f; // 下管道顶 = 间隙下沿
            float gapUpperEdge = up->GetPosition().y - up->GetScale().y * 0.5f;     // 上管道底 = 间隙上沿
            float clearanceLower = (batPos.y - batScale.y * 0.5f) - gapLowerEdge;
            float clearanceUpper = gapUpperEdge - (batPos.y + batScale.y * 0.5f);
            float minClearance = std::min(clearanceLower, clearanceUpper);
            if (minClearance >= 0.0f && minClearance < NearMissThreshold) {
                runNearMissCount++;
                if (fx_) {
                    fx_->AddScore(NearMissBonus);
                    fx_->ReportAchievement(AchievementEvent::NearMissRun, runNearMissCount);
                    fx_->FloatTextAt("贴脸 +" + std::to_string(NearMissBonus),
                                     batPos + Vec2(0.0f, 40.0f),
                                     UITheme::Moonlight.r, UITheme::Moonlight.g,
                                     UITheme::Moonlight.b, 22.0f, 1.0f);
                }
            }
        }

        // 滚动未飞行的管道（飞行中的由 UpdateFlying 接管）
        if (!upFlying) up->SetPosition(newX, up->GetPosition().y);
        if (!downFlying) down->SetPosition(newX, down->GetPosition().y);

        // 摆动变体：间隙中心绕基准值正弦摆动
        // （整列完好时才摆动；残缺列保持静止几何，搭档贴回后下一帧恢复同步）
        if (pairWhole && st.type == 1 && st.oscillAmp > 0.0f) {
            float center = st.baseGapCenter + st.oscillAmp * std::sin(oscillTime * OscillSpeed + st.oscillPhase);
            float minC = SAFE_MARGIN + st.gapSize * 0.5f + terrainY_;
            float maxC = screenH_ - SAFE_MARGIN - st.gapSize * 0.5f;
            center = std::clamp(center, minC, maxC);
            ApplyObstacleGeometry(i, newX, center, st.gapSize);
        }
    }
}

void ObstacleField::RecycleColumns(const StageFrame& f) {
    const float HALF_WIDTH = Config::GetInstance().GetObstacleWidth() * 0.5f;

    // ===== 2. recycle（按实际位置求最左/最右「未飞行」管道；飞行回收会打破计数器顺序） =====
    size_t leftmostIdx = SIZE_MAX;
    bool leftmostIsUp = false;
    float minX = 1e9f;
    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        if (!flyingStates[i].up.flying) {
            float x = obstaclesUpPool[i]->GetPosition().x;
            if (x < minX) { minX = x; leftmostIdx = i; leftmostIsUp = true; }
        }
        if (!flyingStates[i].down.flying) {
            float x = obstaclesDownPool[i]->GetPosition().x;
            if (x < minX) { minX = x; leftmostIdx = i; leftmostIsUp = false; }
        }
    }

    // 段落期（怪物接管）：停止新管道生成，让场上管道自然滚出（玩家视野聚焦怪物战）
    if (suppressPipeSpawn_) return;

    if (leftmostIdx != SIZE_MAX && minX < -(HALF_WIDTH + 0.00001f)) {
        float rightMostX = RightmostLivePipeX(leftmostIdx);
        float spacing = Config::GetInstance().GetSpacingRange().first +
            Random::Get().GetRandomFloat() *
            (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
        float newX = (rightMostX < -9e8f)
            ? screenW_ + 300.0f // 无其它存活列：退到屏幕外右侧
            : rightMostX + spacing;

        // 残缺列（搭档在飞）：只重插在列的那根，搭档稍后贴回；整列完好：整列重插
        const bool partnerFlying = leftmostIsUp ? flyingStates[leftmostIdx].down.flying
                                                : flyingStates[leftmostIdx].up.flying;
        if (partnerFlying) {
            ReinsertFlyingPipe(leftmostIdx, leftmostIsUp, newX);
        } else {
            ReinsertColumn(leftmostIdx, newX);
        }

        // 在「原最右列 -> 新列」之间的走廊随机生成道具
        if (rightMostX > -9e8f && spawnCorridorItem_) {
            spawnCorridorItem_(rightMostX, newX);
        }
    }
}

void ObstacleField::Render(SpriteRenderer& renderer) const {
    renderer.Draw_UV(obstaclesUpPool);
    renderer.Draw_UV(obstaclesDownPool);
}

bool ObstacleField::CheckCollision(Vec2 batPos, Vec2 batScale) const {
    if (obstaclesUpPool.empty()) return false;
    if (!pipesLethal_ || !edgesLethal_)
        return false; // 段落期（怪物接管）：管道/边缘不即死，伤害走怪物专属受击模型
    if (batPos.y - BatSprite::EdgeContactMargin <= terrainY_) return true;     // 碰到地面
    if (batPos.y + BatSprite::EdgeContactMargin >= screenH_) return true;      // 碰到天花板

    size_t num = obstaclesUpPool.size();
    std::vector<SpriteGeometry> upGeom(num);
    std::vector<SpriteGeometry> downGeom(num);

    for (size_t i = 0; i < num; ++i) {
        // 飞行中的管道不参与碰撞判定（已被撞飞）；未飞行的照常判定
        bool upFlying = i < flyingStates.size() && flyingStates[i].up.flying;
        bool downFlying = i < flyingStates.size() && flyingStates[i].down.flying;
        if (upFlying) {
            upGeom[i] = {Vec2(0.0f, 0.0f), Vec2(-10000.0f, -10000.0f)};
        } else {
            upGeom[i] = {obstaclesUpPool[i]->GetScale(), obstaclesUpPool[i]->GetPosition()};
        }
        if (downFlying) {
            downGeom[i] = {Vec2(0.0f, 0.0f), Vec2(-10000.0f, -10000.0f)};
        } else {
            downGeom[i] = {obstaclesDownPool[i]->GetScale(), obstaclesDownPool[i]->GetPosition()};
        }
    }

    SpriteGeometry batGeom = {batScale, batPos};
    return ::CheckCollision(upGeom, batGeom, upCollisionUV, batCollisionUV) ||
           ::CheckCollision(downGeom, batGeom, downCollisionUV, batCollisionUV);
}

int ObstacleField::SmashOverlapping(Vec2 center, float halfSize) {
    int smashed = 0;
    const float OBS_HALF = Config::GetInstance().GetObstacleWidth() * 0.5f;
    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        const auto& fly = flyingStates[i];
        if (fly.up.flying && fly.down.flying) continue; // 整列都在飞
        // 列锚点取未飞行的那根（单管被撞飞后另一根仍在列上）
        float colX = fly.up.flying ? obstaclesDownPool[i]->GetPosition().x
                                   : obstaclesUpPool[i]->GetPosition().x;
        if (std::abs(center.x - colX) > halfSize + OBS_HALF) continue;
        float upBottom = obstaclesUpPool[i]->GetPosition().y - obstaclesUpPool[i]->GetScale().y * 0.5f;
        float downTop = obstaclesDownPool[i]->GetPosition().y + obstaclesDownPool[i]->GetScale().y * 0.5f;
        bool hitUp = !fly.up.flying && (center.y + halfSize > upBottom);     // 上缘探入上管
        bool hitDown = !fly.down.flying && (center.y - halfSize < downTop);  // 下缘探入下管
        if (hitUp || hitDown) {
            SmashObstacles(i, hitUp, hitDown);
            smashed += (hitUp ? 1 : 0) + (hitDown ? 1 : 0);
        }
    }
    return smashed;
}

void ObstacleField::SmashObstacles(size_t i, bool smashUp, bool smashDown) {
    auto& fly = flyingStates[i];
    // 水平初速同侧共用（同一次撞飞的管道向同一方向崩开，视觉成组）
    float dirX = (Random::Get().GetRandomFloat() < 0.5f ? -1.0f : 1.0f) *
                 (160.0f + 220.0f * Random::Get().GetRandomFloat());
    auto rollSpin = []() {
        return (Random::Get().GetRandomFloat() < 0.5f ? -1.0f : 1.0f) *
               (3.5f + 3.0f * Random::Get().GetRandomFloat());
    };
    if (smashUp) {
        fly.up = FlyingPipe{}; // 整体重置，清掉上一轮残留的 angle/life
        fly.up.flying = true;
        fly.up.vel = Vec2(dirX, 320.0f + 260.0f * Random::Get().GetRandomFloat()); // 上管向上抛
        fly.up.spin = rollSpin();
    }
    if (smashDown) {
        fly.down = FlyingPipe{};
        fly.down.flying = true;
        // 下管向下砸（重力会让下管更快离场），制造"轰开"的视觉
        fly.down.vel = Vec2(dirX, -(180.0f + 200.0f * Random::Get().GetRandomFloat()));
        fly.down.spin = rollSpin();
    }

    runSmashCount += (smashUp ? 1 : 0) + (smashDown ? 1 : 0); // 按根计数
    if (fx_) {
        fx_->ReportAchievement(AchievementEvent::SmashRun, runSmashCount);
        fx_->PlaySFX((int)SFX::Thunder, 1.0f); // 撞击音效
        fx_->Shake(12.0f);
        fx_->FloatTextAt("撞飞！", Vec2(
            (obstaclesUpPool[i]->GetPosition().x + obstaclesDownPool[i]->GetPosition().x) * 0.5f,
            (obstaclesUpPool[i]->GetPosition().y + obstaclesDownPool[i]->GetPosition().y) * 0.5f + 70.0f),
            0.95f, 0.75f, 0.4f, 24.0f, 1.0f);
    }
    std::cout << "Obstacle " << i
              << (smashUp && smashDown ? " pair" : (smashUp ? " up pipe" : " down pipe"))
              << " smashed away!" << std::endl;
}

bool ObstacleField::UpdateFlyingPipe(FlyingPipe& fp, const std::shared_ptr<Sprite>& pipe, float dt) {
    fp.life += dt;
    fp.vel.y -= FlyingGravity * dt; // 抛物线重力
    fp.angle += fp.spin * dt;
    pipe->SetPosition(pipe->GetPosition().x + fp.vel.x * dt,
                      pipe->GetPosition().y + fp.vel.y * dt);
    pipe->SetRotation(Vec2(std::cos(fp.angle), std::sin(fp.angle)));
    float alpha = std::clamp(1.6f - fp.life, 0.0f, 1.0f); // 后段淡出
    pipe->SetAlpha(alpha);

    float x = pipe->GetPosition().x;
    bool offscreen = x < -260.0f || x > screenW_ + 320.0f;
    return alpha <= 0.02f || offscreen || fp.life > 2.5f;
}

float ObstacleField::RightmostLivePipeX(size_t excludePair) const {
    float rx = -1e9f;
    for (size_t j = 0; j < obstaclesUpPool.size() && j < flyingStates.size(); ++j) {
        if (j == excludePair) continue;
        if (!flyingStates[j].up.flying)
            rx = std::max(rx, obstaclesUpPool[j]->GetPosition().x);
        if (!flyingStates[j].down.flying)
            rx = std::max(rx, obstaclesDownPool[j]->GetPosition().x);
    }
    return rx;
}

void ObstacleField::UpdateFlying(float dt, const StageFrame& f) {
    screenW_ = f.screenW;
    screenH_ = f.screenH;
    terrainY_ = f.terrainY;

    for (size_t i = 0; i < obstaclesUpPool.size() && i < flyingStates.size(); ++i) {
        auto& fly = flyingStates[i];
        if (!fly.up.flying && !fly.down.flying) continue;

        // 各管独立推进（只撞飞的那根在飞，另一根由 Update 正常滚动）
        bool upDone = fly.up.flying && UpdateFlyingPipe(fly.up, obstaclesUpPool[i], dt);
        bool downDone = fly.down.flying && UpdateFlyingPipe(fly.down, obstaclesDownPool[i], dt);
        if (!upDone && !downDone) continue;

        // 结束的管道先脱离飞行状态，再按搭档状态分流回收
        if (upDone) fly.up.flying = false;
        if (downDone) fly.down.flying = false;

        // 飞行结束且搭档仍在飞：本管先行重插为「新列」（最右存活列右侧），搭档稍后贴回；
        // 同帧双结束时上管先重插、下管随即贴回，等效于旧的整列回收
        auto recycleAsNewColumn = [this, i](bool isUp) {
            float rightMostX = RightmostLivePipeX(i); // 本对两管此刻都非「存活锚点」，天然排除
            float spacing = Config::GetInstance().GetSpacingRange().first +
                Random::Get().GetRandomFloat() *
                (Config::GetInstance().GetSpacingRange().second - Config::GetInstance().GetSpacingRange().first);
            float newX = (rightMostX < -9e8f)
                ? screenW_ + 300.0f
                : rightMostX + spacing;
            ReinsertFlyingPipe(i, isUp, newX);
            if (rightMostX > -9e8f && spawnCorridorItem_) {
                spawnCorridorItem_(rightMostX, newX);
            }
        };

        if (upDone && downDone) {
            recycleAsNewColumn(true);
            RejoinFlyingPipe(i, false); // 下管贴回刚重插的上管
        } else if (upDone) {
            if (fly.down.flying) recycleAsNewColumn(true); // 搭档还在飞：本管先重插
            else RejoinFlyingPipe(i, true);                // 搭档在列：贴回
        } else { // downDone
            if (fly.up.flying) recycleAsNewColumn(false);
            else RejoinFlyingPipe(i, false);
        }
    }
}

void ObstacleField::RejoinFlyingPipe(size_t i, bool isUp) {
    auto pipe = isUp ? obstaclesUpPool[i] : obstaclesDownPool[i];
    auto partner = isUp ? obstaclesDownPool[i] : obstaclesUpPool[i];
    const auto& st = obstacleStates[i];

    float gapCenter;
    if (isUp) {
        // 搭档是下管：其顶面 = 间隙下沿
        gapCenter = partner->GetPosition().y + partner->GetScale().y * 0.5f + st.gapSize * 0.5f;
    } else {
        // 搭档是上管：其底面 = 间隙上沿
        gapCenter = partner->GetPosition().y - partner->GetScale().y * 0.5f - st.gapSize * 0.5f;
    }

    pipe->SetRotation(Vec2(1.0f, 0.0f));
    pipe->SetAlpha(1.0f);
    ApplyObstacleGeometryPipe(i, isUp, partner->GetPosition().x, gapCenter, st.gapSize);
}

void ObstacleField::ReinsertFlyingPipe(size_t i, bool isUp, float newX) {
    RollColumnState(i);
    const auto& st = obstacleStates[i];
    auto pipe = isUp ? obstaclesUpPool[i] : obstaclesDownPool[i];
    pipe->SetRotation(Vec2(1.0f, 0.0f));
    pipe->SetAlpha(1.0f);
    ApplyObstacleGeometryPipe(i, isUp, newX, st.baseGapCenter, st.gapSize);
    SetObstacleTintPipe(i, isUp, st.type);
}

void ObstacleField::RollColumnState(size_t i) {
    const float SAFE_MARGIN = Config::GetInstance().GetSafeMargin();
    const float GAP_SIZE = gapSize_;

    // ---- 抽取变体：普通 / 摆动 / 窄间隙 ----
    auto& st = obstacleStates[i];
    st.type = RollObstacleVariant();
    st.gapSize = (st.type == 2) ? NarrowGapOf(GAP_SIZE) : GAP_SIZE;

    // ---- 平滑随机 gap（衔接上一生成中心，按该变体间隙约束） ----
    float delta = Config::GetInstance().GetGapCenterDeltaRange().first +
        Random::Get().GetRandomFloat() *
        (Config::GetInstance().GetGapCenterDeltaRange().second - Config::GetInstance().GetGapCenterDeltaRange().first);
    lastSpawnedGapCenter += delta;

    float minCenter = SAFE_MARGIN + st.gapSize * 0.5f + terrainY_;
    float maxCenter = screenH_ - SAFE_MARGIN - st.gapSize * 0.5f;
    lastSpawnedGapCenter = std::clamp(lastSpawnedGapCenter, minCenter, maxCenter);

    st.baseGapCenter = lastSpawnedGapCenter;
    // 摆幅不超过间隙一半，保证间隙在摆动过程中始终清晰可通行
    st.oscillAmp = (st.type == 1)
        ? std::clamp(25.0f + 15.0f * Random::Get().GetRandomFloat(), 10.0f, st.gapSize * 0.5f - 16.0f)
        : 0.0f;
    st.oscillPhase = Random::Get().GetRandomFloat() * 6.2831853f;
    st.nearMissChecked = false; // 新生成的障碍物重新允许贴脸判定
}

void ObstacleField::ReinsertColumn(size_t i, float newX) {
    RollColumnState(i);
    const auto& st = obstacleStates[i];
    ApplyObstacleGeometry(i, newX, st.baseGapCenter, st.gapSize);
    SetObstacleTint(i, st.type);
}

int ObstacleField::RollObstacleVariant() const {
    float r = Random::Get().GetRandomFloat();
    // 摆动：第 2 档起，15%+3%/档（上限 35%）；窄间隙：第 4 档起，15%+2%/档（上限 30%）
    float movingProb = (tier_ >= OscillatingUnlockTier)
        ? std::min(0.15f + 0.03f * tier_, 0.35f) : 0.0f;
    float narrowProb = (tier_ >= NarrowUnlockTier)
        ? std::min(0.15f + 0.02f * tier_, 0.30f) : 0.0f;
    if (r < movingProb) return 1;
    if (r < movingProb + narrowProb) return 2;
    return 0;
}

void ObstacleField::ApplyObstacleGeometryPipe(size_t i, bool isUp, float x,
                                              float gapCenter, float gapSize) {
    const float OBS_WIDTH = Config::GetInstance().GetObstacleWidth();
    if (isUp) {
        float upHeight = screenH_ - (gapCenter + gapSize * 0.5f);
        obstaclesUpPool[i]->SetScale(OBS_WIDTH, upHeight);
        obstaclesUpPool[i]->SetPosition(x, screenH_ - upHeight * 0.5f);
    } else {
        float downHeight = gapCenter - gapSize * 0.5f - terrainY_;
        obstaclesDownPool[i]->SetScale(OBS_WIDTH, downHeight);
        obstaclesDownPool[i]->SetPosition(x, downHeight * 0.5f + terrainY_);
    }
}

void ObstacleField::ApplyObstacleGeometry(size_t i, float x, float gapCenter, float gapSize) {
    ApplyObstacleGeometryPipe(i, true, x, gapCenter, gapSize);
    ApplyObstacleGeometryPipe(i, false, x, gapCenter, gapSize);
}

void ObstacleField::SetObstacleTintPipe(size_t i, bool isUp, int type) {
    // 窄间隙管道偏红提示；其余恢复原色
    if (type == 2) {
        (isUp ? obstaclesUpPool[i] : obstaclesDownPool[i])->SetColor(1.0f, 0.76f, 0.76f, 1.0f);
    } else {
        (isUp ? obstaclesUpPool[i] : obstaclesDownPool[i])->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void ObstacleField::SetObstacleTint(size_t i, int type) {
    SetObstacleTintPipe(i, true, type);
    SetObstacleTintPipe(i, false, type);
}
