#pragma once
#include "../Math.h"
#include "../app/Achievements.h"
#include <string>

// =============================================================================
// 表现层服务接口（docs/怪物系统架构设计.md §4.3）：怪物上行反馈的最大公约数集合。
// MainScene 实现（薄转发到既有能力）；怪物不 include 场景、不直连 AudioSystem/UI 状态。
// 收口规则：入接口前先问「是否所有怪物通用」；品牌化细节由怪物自绘（RenderWorld/RenderUI）。
// 命名避让：MainScene 已有嵌套类型 FloatText/WeatherMsg（结构体），成员函数名会遮蔽
// 同名嵌套类型名，故飘字方法取 FloatTextAt、消息盒方法复用场景既有 PostWeatherMsg 之名。
// =============================================================================
struct IStageFeedback {
    virtual ~IStageFeedback() = default;

    // ---- 即时反馈 ----
    // 颜色为 r/g/b 浮点（与场景 SpawnFloatText 同词汇；接口头不引入 UI 主题头，
    // 怪物侧可直接传 UITheme::Amber.r/.g/.b 或字面量）
    virtual void FloatTextAt(const std::string& text, Vec2 pos,
                             float r, float g, float b,
                             float size, float life) = 0;      // 世界坐标飘字
    virtual void PlaySFX(int sfxId, float volume) = 0;         // sfxId 取 AudioSystem::SFX 枚举值
    virtual void Shake(float intensity) = 0;                   // 全局抖动
    // 天气事件消息盒：槽位分配见 IEnemy.h kEnemyWeatherMsgSlotBase
    virtual void PostWeatherMsg(int slot, const std::string& text,
                                float flashTime, bool autoSteady) = 0;
    virtual void RemoveWeatherMsg(int slot) = 0;

    // ---- 结算通道（场景侧自动叠加模式倍率/存档/成就上报等既有规则）----
    virtual void AddScore(size_t base) = 0;      // 走 AddScore 统一入口（模式倍率/成就自动生效）
    virtual void AddCoins(int n) = 0;            // 入账并落盘
    virtual void AddTimeLimit(float sec) = 0;    // 限时模式奖励时长（非限时为空操作）
    virtual void ReportAchievement(AchievementEvent event, long long value) = 0;

    // ---- 对玩家的权威操作（唯一入口）----
    virtual void KillPlayer(const char* reason) = 0; // 统一死亡流程（怪物复位+音效+死亡动画+结算）
    virtual void BounceBat(float dx, float dy) = 0;  // 弹起/击退位移（俯冲撞击向左弹开用）
    virtual void OnEnemyItemCollected(Vec2 pos) = 0; // 怪物道具被拾取（连击/成就通道，预留）
    virtual void EndGiant() = 0;                     // 终止巨化（巨化冲撞命中怪物后由怪物侧申请）
    virtual void GrantBatInvuln(float dur) = 0;      // 蝙蝠受击无敌授予（闪烁反馈；不缩短既有更长无敌）
    virtual void TriggerSlowMo(float dur) = 0;       // 击杀慢动作（整帧时间缩放，线性缓升恢复）
};
