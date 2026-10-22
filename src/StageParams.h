#pragma once
#include "GameMode.h"

// =============================================================================
// 模式参数表：玩法 / 天气差异的唯一事实来源。
//
// MainScene 以 using 别名保留 MainScene::ModeParams 的既有引用方式；
// WeatherDirector 等子系统直接读本结构，不再反向依赖场景头。
// 表本体（ParamsOf）由 MainScene.cpp 提供——保持了"模式差异集中登记"的单一事实源。
// =============================================================================
constexpr float kTimeRushDuration = 60.0f; // 限时冲刺时长（各模式 Reset 的倒计时初值）
constexpr int kMaxDifficultyTier = 10;     // 难度档位上限

struct ModeParams {
    const char* name;          // 模式名（日志用）
    float speedFactor;         // 障碍物速度倍率（乘在难度递进曲线上）
    int   scoreFactor;         // 得分倍率（风暴之夜双倍）
    const char* scoreEvent;    // 模式专属成就上报事件（nullptr = 无）
    float gustIntervalMin;     // 阵风间隔下限
    float gustIntervalMax;     // 阵风间隔上限
    float gustForceFactor;     // 阵风力度倍率
    bool  stormWeather;        // true = 风暴天气常驻（跳过难度档位曲线）
    float stormRain;           // 常驻雨强（stormWeather 时）
    float stormLightningMin;   // 常驻落雷间隔下限（stormWeather 时）
    float stormLightningMax;   // 常驻落雷间隔上限（stormWeather 时）
    float stormScrim;          // 常驻天色遮罩透明度（stormWeather 时）
    bool  timed;               // 是否限时
    float timeLimit;           // 限时秒数（timed=false 时仅作为 Reset 初值）
    float comboHudY;           // 连击 HUD 的 y（限时模式顶部让位给倒计时）
    float lightningWarnLead;   // 落雷预警提前量：雷劈下前的虚线预警时长（经典最长，随难度递减）
};

// 参数表实现见 MainScene.cpp（唯一登记点）
const ModeParams& ParamsOf(GameMode m);
