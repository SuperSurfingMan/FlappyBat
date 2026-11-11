#pragma once
#include <cstddef>

class RunFlow;
class ScoreRank;
class PlayerProfile;

// =============================================================================
// 局末结算规则（第 4 期从 MainScene::SettleRunResults 抽出）。
//
// 抽出的动机：结算原本混在场景里，既算规则（破纪录判定 / 金币换算 / 写榜）
// 又做表现（破纪录音效 / 金币飘字 / 弹出结算面板），无法单独验证。
// 现在这里只做「规则与落盘」，把结果以值对象返回；场景负责把它演出来。
//
// 依赖以引用传入而非取全局访问点：结算可在自测里用临时榜单与临时存档驱动。
// =============================================================================
struct SettlementResult {
    std::size_t score = 0;      // 本局最终分数
    int  best = 0;              // 写入榜单后的当前模式最高分
    bool newRecord = false;     // 本局是否刷新了该模式历史最高分
    long long coinBonus = 0;    // 本局结算追加的金币（= 分数 / 5）
};

class RunSettlement {
public:
    // 金币换算比例：每 5 分折 1 金币（B8）
    static constexpr std::size_t ScorePerCoin = 5;

    // 依次完成：破纪录判定 -> 写榜 -> 标记已保存 -> 金币结算并落盘
    // 顺序不可调换：破纪录必须在写榜之前判定，否则本局成绩会与自己比较。
    static SettlementResult Apply(RunFlow& flow, ScoreRank& scores, PlayerProfile& profile);
};
