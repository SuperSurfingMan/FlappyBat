#pragma once
#include "enemy/IEnemy.h"
#include <memory>
#include <string>
#include <vector>

// 内置怪物登记表（EnemyRoster.cpp）：新增怪物的唯一登记点（docs/怪物系统架构设计.md §9）
void RegisterBuiltinEnemies(class EnemyDirector& director);

// 启动期校验：每个内置怪物的「按模式调参表」都必须覆盖全部 GameMode。
// 新增模式是最容易漏补这些表的地方；与登记点同文件维护，新增怪物时在此补一行。
bool ValidateEnemyTunings(std::string* firstError = nullptr);

// =============================================================================
// 怪物导演：场景与怪物之间的唯一通道（docs/怪物系统架构设计.md §4.4）。
// 职责收敛为三件事：
//   驱动 —— 按注册序推进（暂停/入场/死亡的冻结由场景门控 Update 调用保证）；
//   聚合 —— 段落规则取注册序第一个接管中的怪物（单怪不变量为 BOSS设计文档 §3 硬性规则）；
//   编排 —— 世界层/前景层按注册序在固定槽位渲染，场景不感知怪物层级。
// 导演不认识任何具体怪物，不持有玩法逻辑。
// =============================================================================
class EnemyDirector {
public:
    void Register(std::shared_ptr<IEnemy> enemy);

    void InitAll();     // 资源就绪后建精灵/对象池
    void OnRunStart();  // 重开一局：全员完全复位
    void OnBatDead();   // 蝙蝠死亡：全员即时复位（段落中止）

    // 每帧驱动；ctx.fxFx / ctx.fxEconomy / ctx.fxPlayer 须由调用方填好（MainScene 实现三个窄接口）。
    // 调用结束后 Rules() 即为当帧聚合结果。
    void Update(float dt, const EnemyContext& ctx);

    const EncounterRules& Rules() const { return rules_; }

    void RenderWorld(SpriteRenderer& renderer); // 世界层槽位：道具之后、风线/蝙蝠之前
    void RenderUI();                            // 前景层槽位：场景 ImGui 布局内

private:
    std::vector<std::shared_ptr<IEnemy>> enemies_;
    EncounterRules rules_{}; // 默认=世界规则无接管
};
