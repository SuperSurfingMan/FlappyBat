#pragma once
#include <vector>
#include <random>
#include <memory>
#include <unordered_set>
#include "core/Math.h"
#include "core/Timer.h"

#include "render/Animation.h"
#include "sprite/Sprite.h"
#include "core/Config.h"

class SpriteRenderer;

struct RainDrop
{
    Vec2 position;
    float speed;
    float length;

    float depth;   // 0 = far , 1 = near

    float maskHeight = 0.f;
    Vec2 hit_point;
    bool isFading = false;
};

struct LightningBranch
{
    std::vector<Vec2> points;
    float thickness;
};

struct Lightning
{
    LightningBranch mainBranch;
    std::vector<LightningBranch> subBranches;
};

struct RainInstance
{
    Vec2 position;
    Vec2 size;
    float rotation;
    float maskHeight;
};

struct Points {
    Vec2 left;
    Vec2 right;

    Points operator+(const Points& other) const {
        return Points{left + other.left, right + other.right};
    }
};

// 雨/闪电的纯逻辑系统：只负责雨滴、闪电、水花的数据更新，不接触任何 GL。
// 渲染由 RainRenderer / LightningRenderer 负责，通过只读 getter 读取数据。
class RainSystem
{
public:
    RainSystem() = default;
    ~RainSystem() = default;

    void Update(float dt);
    void Reset();

    void StartRandomLightning(float interval_min, float interval_max);

    // 地面滚动速度（水花漂移、地形累计偏移跟随该速度；难度递进后与障碍物速度同步）
    void SetScrollSpeed(float speed) { scrollSpeed = speed; }

    // ===== 雷击威胁（A4-a）：先预警后落雷 =====
    // 两条独立状态轴（脚本雷与随机预警互不覆盖的契约）：
    //   预警轴 lightningPhase —— 仅随机调度链路：None（空闲）-> Warning（预警挂起，虚线/包围盒绘制中）
    //   雷体轴 isLightningActive —— 随机落雷与脚本落雷（TriggerStrikeAt）共用，雷体渲染/危险判定期间为 true
    // 不变式：Warning 挂起期间必无雷体（调度到点遇雷体则顺延；脚本雷会终结挂起预警），
    //         雷体期间 phase 恒为 None——预警虚线与实际雷线因此永不同屏。
    enum class LightningPhase { None, Warning };
    LightningPhase GetLightningPhase() const { return lightningPhase; } // 预警挂起状态（雷体渲染另见 IsStrikeDangerous）
    float GetStrikeX() const { return strikeX; }
    float GetWarningTimer() const { return warningTimer; }
    float GetLightningWarnDuration() const { return lightningWarnDuration; } // 预警提前量（随模式设置）
    // 落雷后的危险窗口内，处于落点区域的蝙蝠会被击中（与预警相位解耦：脚本雷可发生于任何相位）
    bool IsStrikeDangerous() const { return isLightningActive && strikeDangerTimer > 0.0f; }

    // 距下一次落雷「预警」开始的剩余秒数（仅相位 None 且定时器计时中有效；否则返回 -1）
    // 供消息盒提前预警轮询使用（天气事件消息盒）
    float GetTimeToNextLightning() const;

    // 主分支相对落点的最大横向漂移（生成钳制与预警矩形共用，保证矩形与实际威胁区一致）
    static constexpr float StrikeDriftClamp = 120.0f;

    // 主分支第 pointIndex 个节点处的雷线全宽（与 LightningRenderer 的锥形收细公式一致：
    // 落点处最粗，向天顶收细）。命中判定宽度以此为准。
    float GetMainBranchWidthAt(size_t pointIndex) const;

    // ===== 天气联动（A4-c） =====
    void SetLightningIntervals(float minSec, float maxSec); // 影响下一次及之后的落雷间隔
    void SetLightningWarnLead(float sec); // 预警提前量（随模式差异化；同时抬高落雷间隔下限）
    // 怪物/脚本化落雷（B5 雷暴召唤）：跳过随机调度与预警相，立即在指定落点落雷
    // （预警虚线由调用方先行展示；危险窗/音效/渲染沿用既有管线。
    //   若随机预警挂起，该次预警被本次落雷取代，下一次随机预警按既有调度链到来；
    //   调度链到点时若雷体仍在渲染，预警自动顺延至雷体结束——见 EnterLightningWarning）
    void TriggerStrikeAt(float strikeX);
    void SetRainIntensity(float t);                          // 0~1，渐变过渡
    size_t GetActiveDropCount() const { return activeDropCount; }

    // 分步初始化（数据部分，供增量加载调用）
    void InitStepRains();
    void InitStepSplashSets();

    // ===== 只读数据访问（供渲染器使用） =====
    const std::vector<RainDrop>& GetDrops() const { return rainDrops; }
    float GetRainWidth() const { return rainWidth; }
    float GetWind() const { return wind; }

    bool IsLightning() const { return isLightning; }
    const Lightning& GetLightning() const { return currentLightning; }
    float GetLightningExposure() const { return lightningExposure; }
    float GetLightningTimeCounter() const { return lightningTimeCounter; }

    const std::unordered_set<std::shared_ptr<Sprite>>& GetActiveSplashes() const { return rainSplashSpritesActive; }

private:
    void InitRains(int count, float screenWidth, float screenHeight);
    void InitSplashSets(size_t set_size);

private:
    std::vector<RainDrop> rainDrops;

    float screenWidth = 0.f;
    float screenHeight = 0.f;

    float rainWidth = 1.5f;       // 雨线宽度
    float wind = 200.0f;          // 横向风速

private:
    void UpdateRains(float dt);

    void GenerateLightning(float strikeX); // 自落点向上生成，与预警光柱锚定同一位置
    void GenerateBranches(const LightningBranch& parent, int depth);
    void EnterLightningWarning();
    void BeginLightningStrike();
    void RespawnDrop(RainDrop& drop); // 把雨滴重置到屏幕上方（动态增雨时使用）

    Lightning currentLightning;
    Timer lightningTimer;
    Vec2 lightningTime;

    bool isLightningActive = false;
    bool isLightning = false;
    const float lightningLastTime = 1.0f;
    float lightningTimeCounter = 0.f;

    // 预警 -> 落雷 状态机（A4-a；两轴契约见类内 LightningPhase 声明处）
    LightningPhase lightningPhase = LightningPhase::None;
    float strikeX = 0.0f;            // 本次落雷的落点 x（预警与落雷共用；脚本雷会覆写）
    float warningTimer = 0.0f;       // 预警剩余时间
    float strikeDangerTimer = 0.0f;  // 落雷危险窗口剩余时间（跟随雷体轴计时，与相位无关）
    float lightningWarnDuration = 0.8f;  // 预警提前量（默认 0.8s；MainScene 按模式经 SetLightningWarnLead 设置）
    static constexpr float LightningDangerDuration = 0.4f; // 落雷后可击中的窗口
    bool deferredWarning = false;    // 调度到点时雷体仍在渲染：预警顺延标志（雷体结束即刻进入预警，不丢节奏）

    // 可调落雷间隔（A4-c：随难度档位收窄）
    float intervalMin = 5.0f;
    float intervalMax = 10.0f;

    // 雨强（A4-c）：目标值渐变过渡，仅激活前 activeDropCount 个雨滴参与更新/渲染
    float rainIntensity = 1.0f;
    float targetRainIntensity = 1.0f;
    size_t activeDropCount = 0;

private:
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> gapRand{0.0f, 1.0f};
    float Random(float min, float max);

private:
    float lightningExposure = 0.0f;
    float lightningFlickerTimer = 0.3f;

private:
    float terrain_height = 480.0f - 381.0f;
    float scrollSpeed = 200.0f; // 地面滚动速度（默认与基础障碍物速度一致）

    std::unordered_set<std::shared_ptr<Sprite>> rainSplashSpritesActive;
    std::unordered_set<std::shared_ptr<Sprite>> rainSplashSpritesInactive;

    void UpdateSplashSprites(float dt);
    void SpawnSplash(Vec2 position, Vec2 rotation);

private:
    float terrain_accumulated_offset = 0.f;
    void UpdateTerrainAccumOffset(float dt);
    // 根据 x -> 地形表面区间端点 left + right
    Points GetEdgePoints(float x);
    Points GetEdgePointsHelper(float x);
};
