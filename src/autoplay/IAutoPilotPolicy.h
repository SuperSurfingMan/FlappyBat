#pragma once
// =============================================================================
// Auto Play 策略契约（自动演示模块，开闭原则扩展点之一）
//
// 一局是否由 AI 操控由两层决定：
//   1. 设置页开关（AutoPilotDirector::Enabled，持久化统一存档 U 行）；
//   2. 当前对局模式是否登记了策略（Director 按 ModeKey 查注册表）。
// 两层同时满足 = "武装（Armed）"，MainScene 每帧向 Director 要一次决策，
// 把返回的动作意图挂到 StageFrame::autoPilot 下传——与人类输入走同一消费点
// （振翅音效/边界钳制/无敌护盾能量闸门/短护盾 CD 闸门全部复用），未武装时指针为 null，
// 行为与既有版本完全一致。
//
// 新增一个可自动演示的模式：在 AutoPilotRoster.cpp 为该 ModeKey 注册一个
// IAutoPilotPolicy 实现即可，Director/MainScene/PlayerAbilities 零改动。
// =============================================================================
#include "autoplay/AutoPilotSenses.h"

// ===== 策略每帧输出的动作意图（语义等同"一个虚拟玩家的本帧输入"）=====
struct AutoPilotActions {
    bool jump = false;         // 本帧振翅（等效按下振翅键，走既有边沿路径含音效）
    int  horizontal = 0;       // 横移方向：-1 左 / 0 保持 / +1 右（持续语义，每帧覆写）
    bool firePrimary = false;  // 主攻击（等效 F/右键；枭王段落期 = 音爆）
    bool releaseInvincible = false;   // 释放无敌护盾（能量闸门在 PlayerAbilities 内）
    bool releaseShortShield = false;  // 释放一次性短护盾（5s CD 闸门在 PlayerAbilities 内）
};

// ===== 策略接口：只读感知快照 -> 本帧动作意图 =====
class IAutoPilotPolicy {
public:
    virtual ~IAutoPilotPolicy() = default;

    virtual const char* Name() const = 0;

    // 每帧一次（紧贴 player.Update 之前调用，感知即当帧世界状态）。
    // 纯决策：不得触碰任何引擎/场景对象，只读 senses、返回意图（const：策略无状态）。
    virtual AutoPilotActions Decide(const AutoPilotSenses& senses, float dt) const = 0;
};
