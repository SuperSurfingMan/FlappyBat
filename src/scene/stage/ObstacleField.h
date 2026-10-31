#pragma once
#include "scene/stage/StageFrame.h"
#include "core/Math.h"
#include "core/Point2D.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

class Sprite;
class SpriteRenderer;
struct IStageFeedback;

// =============================================================================
// 障碍场：管道对象池 / 变体（普通·摆动·窄间隙）/ 滚出回收 / 贴脸与计分 /
// 被巨化撞飞后的抛物线飞行与回收 / 与蝙蝠的碰撞采样。
//
// 边界：
//   - 只读 StageFrame（难度、速度、怪物段落规则），不读场景字段；
//   - 上行反馈经 IStageFeedback（计分/成就/音效/飘字/抖动）；
//   - 走廊道具生成通过回调交给 ItemField，障碍不认识道具池。
// =============================================================================
class ObstacleField {
public:
    void Load();
    void Reset(const StageFrame& f);
    void Update(float dt, const StageFrame& f);
    void UpdateFlying(float dt, const StageFrame& f);
    void Render(SpriteRenderer& renderer) const;

    // 蝙蝠 vs 管道/上下边界（edgesLethal 时判边界）
    bool CheckCollision(Vec2 batPos, Vec2 batScale) const;

    // 巨化冲撞：撞飞与体型重叠的管道（返回本次撞飞根数）
    int SmashOverlapping(Vec2 center, float halfSize);

    void SetFeedback(IStageFeedback* fx) { fx_ = fx; }
    void SetCorridorItemSpawner(std::function<void(float, float)> cb) {
        spawnCorridorItem_ = std::move(cb);
    }

    int RunSmashCount() const { return runSmashCount; }
    int RunNearMissCount() const { return runNearMissCount; }

private:
    struct VariantState {
        int type = 0;               // 0 普通 / 1 摆动 / 2 窄间隙
        float gapSize = 100.0f;     // 该对障碍物的间隙大小
        float baseGapCenter = 0.0f; // 摆动基准中心
        float oscillAmp = 0.0f;     // 摆幅（不超过间隙一半，避免道具被管道吞掉）
        float oscillPhase = 0.0f;   // 摆动初相位
        bool nearMissChecked = false;
    };
    struct FlyingPipe {
        bool flying = false;
        Vec2 vel;
        float spin = 0.0f;
        float angle = 0.0f;
        float life = 0.0f;
    };
    struct FlyingState {
        FlyingPipe up;
        FlyingPipe down;
    };

    // ===== 更新步骤 =====
    void ScrollAndScore(float dt, const StageFrame& f);
    void RecycleColumns(const StageFrame& f);
    void RollColumnState(size_t i);
    void ReinsertColumn(size_t i, float newX);
    void ApplyObstacleGeometry(size_t i, float x, float gapCenter, float gapSize);
    void ApplyObstacleGeometryPipe(size_t i, bool isUp, float x, float gapCenter, float gapSize);
    void SetObstacleTint(size_t i, int type);
    void SetObstacleTintPipe(size_t i, bool isUp, int type);
    int  RollObstacleVariant() const;

    // ===== 飞行与回收 =====
    bool UpdateFlyingPipe(FlyingPipe& fp, const std::shared_ptr<Sprite>& pipe, float dt);
    float RightmostLivePipeX(size_t excludePair) const;
    void RejoinFlyingPipe(size_t i, bool isUp);
    void ReinsertFlyingPipe(size_t i, bool isUp, float newX);
    void SmashObstacles(size_t i, bool smashUp, bool smashDown);

    // ===== 状态 =====
    static constexpr int PoolSize = 5;
    static constexpr int OscillatingUnlockTier = 2;  // 摆动管道解锁档位
    static constexpr int NarrowUnlockTier = 4;       // 窄间隙管道解锁档位
    static constexpr float OscillSpeed = 1.8f;       // 摆动角速度（rad/s）
    static constexpr float NarrowGapFactor = 0.78f;  // 窄间隙相对当前间隙的比例
    static constexpr float MinNarrowGap = 74.0f;     // 窄间隙下限（蝙蝠 64px + 余量）
    static constexpr float NearMissThreshold = 12.0f; // 越过管道时与边缘的最小间隙阈值
    static constexpr int NearMissBonus = 2;
    static constexpr float FlyingGravity = 700.0f;

    // 窄间隙的实际间隙：当前间隙 x0.78，但下限不越过当前间隙本身——
    // 高难度档（9 档起）当前间隙已低于 MinNarrowGap，此时窄间隙不再进一步收缩，
    // 否则 std::clamp(x, 74, 73) 上下界倒挂直接断言崩溃（击杀奖励跳档后必现）
    static float NarrowGapOf(float currentGap) {
        return std::max(currentGap * NarrowGapFactor, std::min(currentGap, MinNarrowGap));
    }

    std::vector<std::shared_ptr<Sprite>> obstaclesUpPool;
    std::vector<std::shared_ptr<Sprite>> obstaclesDownPool;
    std::array<VariantState, PoolSize> obstacleStates;
    std::array<FlyingState, PoolSize> flyingStates;
    std::array<Point2D, 3> upCollisionUV;
    std::array<Point2D, 3> downCollisionUV;
    std::array<Point2D, 2> batCollisionUV;

    float oscillTime = 0.0f;
    float lastSpawnedGapCenter = 0.0f;

    // 当前帧快照（避免内部辅助函数层层传参）
    int tier_ = 0;
    float gapSize_ = 100.0f;
    float screenW_ = 540.0f;
    float screenH_ = 960.0f;
    float terrainY_ = 0.0f;
    bool freezeOffscreenPipes_ = false;
    bool suppressPipeSpawn_ = false;
    bool pipesLethal_ = true;
    bool edgesLethal_ = true;

    int runSmashCount = 0;
    int runNearMissCount = 0;

    IStageFeedback* fx_ = nullptr;
    std::function<void(float, float)> spawnCorridorItem_;
};
