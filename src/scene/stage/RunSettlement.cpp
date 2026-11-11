#include "scene/stage/RunSettlement.h"

#include "app/PlayerProfile.h"
#include "app/ScoreRank.h"
#include "scene/stage/RunFlow.h"

#include <algorithm>

SettlementResult RunSettlement::Apply(RunFlow& flow, ScoreRank& scores, PlayerProfile& profile) {
    SettlementResult r;
    r.score = flow.Score();

    // 破纪录判定必须在写榜之前：写榜后本局成绩已在榜单里，与自己比较永远不成立
    const std::vector<size_t> before = scores.GetScores(flow.Mode());
    r.newRecord = before.empty() || r.score > *std::max_element(before.begin(), before.end());

    scores.UpdateScore(r.score, flow.Mode());
    flow.MarkScoreSaved(); // 已保存，退出场景时不再重复保存

    // 金币结算：分数折算后入账并落盘（B8）
    r.coinBonus = (long long)(r.score / ScorePerCoin);
    profile.AddCoins(r.coinBonus);
    profile.Save();

    // 结算面板显示的「最高分」取写榜后的榜首
    const std::vector<size_t> after = scores.GetScores(flow.Mode());
    r.best = after.empty() ? (int)r.score : (int)after.front();
    return r;
}
