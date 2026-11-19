#pragma once
#include "autoplay/IAutoPilotPolicy.h"

// =============================================================================
// 经典模式规则策略（内置唯一策略；注册于 AutoPilotRoster.cpp）。
//
// 设计：滚动时域（receding horizon）规则控制，每帧整帧重算，无跨帧状态——
//   横向：威胁优先级瀑布（B5 雷暴走廊 > B6 标记列 > 落雷走廊 > 到达时间微调 >
//         驻留带死区），横向位置同时是"购买反应时间"的旋钮；
//   纵向：小步长仿真 2.4s 视界，评估一组闭环 mini-controller 候选
//         （滑翔/多相位跟踪/激进爬升），以全程对垂直危险集的最小间距评分取优；
//   攻击：交战期充能就绪且与本体纵向对齐时发射音爆（无危险窗口内）；
//   应急：最优计划仍将撞上危险时——能量满则放无敌护盾，否则 CD 就绪则放短护盾。
//
// 纯函数式决策（只读 senses，无内部状态），可脱离引擎单独构造与验证。
// 与 OwlKing/ObstacleField 的数值对应关系一律经感知快照携带（波速/缺口半宽等），
// 不复制生产方内部常量；少数判定半径为规划余量（标注"规划余量"处可调）。
// =============================================================================
class ClassicRulePilot : public IAutoPilotPolicy {
public:
    const char* Name() const override { return "classic_rule"; }
    AutoPilotActions Decide(const AutoPilotSenses& s, float dt) const override;
};
