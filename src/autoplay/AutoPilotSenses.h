#pragma once
// =============================================================================
// Auto Play 感知快照（AI 的只读世界模型；纯数据，不含任何引擎/场景对象）
//
// 与 StageFrame / EnemyContext 同构的契约头：生产方各自新增 Capture* 只读
// 填充方法（ObstacleField / WeatherDirector / MainScene），策略只消费本头
// 里的结构——因此策略可以脱离引擎单独构造与验证，也不会反向依赖任何子系统。
//
// 坐标系与玩法一致：GL 世界坐标，y 向上为正；地形线 terrainY 之下为地面。
// =============================================================================
#include "core/Math.h"
#include "gameplay/ModeDefinition.h"
#include "enemy/IEnemy.h"   // EnemySense：怪物威胁快照（上行契约归怪物域所有）
#include <vector>

// ===== 蝙蝠本体（MainScene 从 BatSprite / PlayerAbilities 只读接口填充）=====
struct BatSense {
    Vec2 pos{};              // 中心位置
    Vec2 scale{};            // 当前体型（巨化期已放大）
    float vy = 0.f;          // 垂直速度（上正）
    float hSpeed = 0.f;      // 横向速度（本帧将被意图覆写，仅供感知）
    float invincibleEnergy = 0.f; // 无敌护盾充能 0..1（满 = 可释放）
    bool shortShieldReady = false;  // 一次性短护盾 CD 归零、可释放
    bool shortShieldActive = false; // 一次性短护盾生效中（0.5s 内）
    bool invincible = false; // 任意无敌中（无敌护盾/短护盾/巨化/开局保护/受击无敌）
    bool giant = false;      // 巨化期
};

// ===== 阵风（WeatherDirector 只读接口填充；四向推力均折算为漂移速度作用于蝙蝠）=====
struct GustSense {
    Vec2 force{};            // 生效期推力（预警期为 0；0 = 无阵风）
    bool active = false;     // 生效期
    bool warning = false;    // 预警期（消息盒阶段）
    float remaining = 0.f;   // 生效剩余秒（供规划器把外力纳入仿真视界）
};

// ===== 落雷（MainScene 经 WeatherDirector 从 RainSystem 只读接口填充）=====
// 预警期雷折线已预生成且与落雷渲染/命中判定三方共用（"所见即所判"），
// 观察者可在预警期就精确推算雷线走廊。
struct LightningSense {
    bool warning = false;    // 预警挂起（虚线期；落雷未至）
    bool dangerActive = false;// 落雷危险窗内（雷体可击中）
    float strikeX = 0.f;     // 落点 x（预警与落雷共用）
    float warnRemaining = 0.f;// 预警剩余秒
    std::vector<Vec2> boltPoints;   // 主折线（自地面向天顶，points[0] 在地面附近）
    std::vector<float> boltWidths;  // 与 points 一一对应的判定全宽（落点处最粗）
};

// ===== 管道列（ObstacleField 每帧填充；按 x 升序，含屏外右侧即将入画的列）=====
// 间隙上下沿与命中几何同源（上管底/下管顶）；摆动列附摆动参数供预测。
struct PipeSense {
    float x = 0.f;           // 列锚点 x（整列完好取上管，残缺列取在列的那根）
    float halfWidth = 0.f;   // 管道半宽
    float gapTop = 0.f;      // 间隙上沿 y（上管底缘）
    float gapBottom = 0.f;   // 间隙下沿 y（下管顶缘）
    float baseGapCenter = 0.f;// 摆动基准中心
    float oscAmp = 0.f;      // 摆幅（0 = 不摆动）
    float oscPhase = 0.f;    // 摆动初相位
    float oscSpeed = 0.f;    // 摆动角速度 rad/s
    float gapSize = 0.f;     // 该列间隙大小（窄间隙变体已收缩）
    int type = 0;            // 0 普通 / 1 摆动 / 2 窄间隙
    bool upSolid = false;    // 上管在位（被巨化撞飞期间不在位，不构成威胁）
    bool downSolid = false;  // 下管在位
};

// ===== 聚合快照（MainScene 每帧构造一次，紧贴 player.Update 之前）=====
struct AutoPilotSenses {
    // 世界几何与节奏
    float screenW = 0.f;
    float screenH = 0.f;
    float terrainY = 0.f;
    float worldSpeed = 0.f;  // 障碍/世界滚动速度（管道随该速度左移）
    float gravity = 500.f;   // 蝙蝠物理（来自 Config，经 BatSprite 暴露）
    float jumpStrength = 250.f;
    float safeMargin = 100.f;// 管道生成安全边距（摆动预测的 clamp 复用同源公式）
    float oscClock = 0.f;    // 摆动变体全局时钟（ObstacleField 内部时钟快照）
    // 蝙蝠运动边界与横移性能（与 BatSprite 装载的常量同源填充，策略不复制数值）
    float hMoveSpeed = 200.f;// 满速横移速度（BatSprite::HorizontalMoveSpeed）
    float xMin = 40.f;       // 横向活动左边界
    float xMax = 500.f;      // 横向活动右边界
    float edgeMargin = 10.f; // 触边判定半高（BatSprite::EdgeContactMargin）

    BatSense bat;
    GustSense gust;
    LightningSense lightning;
    std::vector<PipeSense> pipes;
    EnemySense boss;         // present=false = 无怪物威胁（或怪物未实现快照）

    ModeKey mode = kDefaultModeKey;
};
