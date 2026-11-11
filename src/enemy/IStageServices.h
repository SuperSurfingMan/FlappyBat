#pragma once
#include "core/Math.h"
#include <cstddef>
#include <string>
#include <string_view>

// =============================================================================
// 表现层服务接口（第 3 期按接口隔离原则拆分，第 4 期完成迁移）。
//
// 原 IStageFeedback 是 16 个纯虚方法的胖接口：任何新实现者都被迫实现全部
// 方法，哪怕它只需要飘字。现在按职责拆成三个角色接口：
//   IFeedbackFx      —— 纯表现（飘字 / 音效 / 抖动 / 消息盒 / 落雷）
//   IRunEconomy      —— 结算通道（计分 / 金币 / 限时 / 成就上报）
//   IPlayerCommands  —— 对玩家的权威操作（致死 / 弹开 / 无敌 / 巨化 / 慢动作）
// 消费者只依赖自己真正用到的那个窄接口：
//   PlayerAbilities -> IFeedbackFx
//   ObstacleField   -> IFeedbackFx + IRunEconomy
//   怪物（OwlKing）  -> 三者按需（EnemyContext 各给一个指针）
// 组合接口 IStageFeedback 已随旧调用点一并移除——不再有「一次实现 16 个方法」
// 的强制耦合。
// =============================================================================
struct IFeedbackFx {
    virtual ~IFeedbackFx() = default;

    // 颜色为 r/g/b 浮点（与场景 SpawnFloatText 同词汇）
    virtual void FloatTextAt(const std::string& text, Vec2 pos,
                             float r, float g, float b,
                             float size, float life) = 0;
    virtual void PlaySFX(int sfxId, float volume) = 0;
    virtual void Shake(float intensity) = 0;
    // 天气事件消息盒：槽位分配见 IEnemy.h kEnemyWeatherMsgSlotBase
    virtual void PostWeatherMsg(int slot, const std::string& text,
                                float flashTime, bool autoSteady) = 0;
    virtual void RemoveWeatherMsg(int slot) = 0;
    // 脚本落雷申请：经场景转发到雨系统管线，怪物不直连场景类型
    virtual void RequestLightningStrike(float worldX) = 0;
};

struct IRunEconomy {
    virtual ~IRunEconomy() = default;

    virtual void AddScore(size_t base) = 0;   // 走统一入口（模式倍率/成就自动生效）
    virtual void AddCoins(int n) = 0;         // 入账并落盘
    virtual void AddTimeLimit(float sec) = 0; // 限时模式奖励时长
    virtual void ReportAchievement(std::string_view eventKey, long long value) = 0;
};

struct IPlayerCommands {
    virtual ~IPlayerCommands() = default;

    virtual void KillPlayer(const char* reason) = 0; // 统一死亡流程
    virtual void BounceBat(float dx, float dy) = 0;
    virtual void OnEnemyItemCollected(Vec2 pos) = 0;
    virtual void EndGiant() = 0;
    virtual void GrantBatInvuln(float dur) = 0;
    virtual void TriggerSlowMo(float dur) = 0;
};
