#pragma once
#include "core/Math.h"
#include "ui/GameInput.h"
#include "autoplay/IAutoPilotPolicy.h" // AutoPilotActions：Auto Play 动作意图契约

// =============================================================================
// 局内子系统的只读帧快照（MainScene 每帧填充一次）。
//
// 设计边界：
//   - 只放"跨子系统都需要、且不需要写"的数据；
//   - 怪物规则已摊平为布尔/数值，子系统不认识 EnemyDirector，
//     避免场景子系统反向依赖怪物框架；
//   - dt 是已应用慢动作缩放后的真实步长。
// =============================================================================
struct StageFrame {
    float dt = 0.0f;
    float screenW = 0.0f;
    float screenH = 0.0f;
    float terrainY = 0.0f;

    Vec2 batPos{};
    Vec2 batScale{};
    bool batInvincible = false;
    bool batGiant = false;

    int   tier = 0;             // 当前难度档位
    float obstacleSpeed = 0.0f; // 当前障碍/世界滚动速度
    float gapSize = 0.0f;       // 当前档位间隙

    // ---- 怪物段落规则（EncounterRules 摊平，方向：怪物 -> 场景）----
    bool freezeOffscreenPipes = false;
    bool suppressPipeSpawn = false;
    bool suppressItemSpawn = false;
    bool pauseGust = false;
    float enemyScrimBoost = 0.0f; // 怪物段落压暗增量（scrimBoost）
    float enemyMinRain = 0.0f;    // 怪物段落雨强下限（minRainIntensity）

    const GameInputFrame* input = nullptr;

    // Auto Play 动作意图（非空 = 已武装）：本帧玩法输入由策略提供，
    // 玩家子系统在各输入读取点优先消费它；null = 人类输入直通（行为不变）。
    // 意图与人类输入走同一消费点，音效/无敌帧/冷却/能量闸门全部复用。
    const AutoPilotActions* autoPilot = nullptr;

    float timeLeft = 0.0f;
    bool  timed = false;
};
