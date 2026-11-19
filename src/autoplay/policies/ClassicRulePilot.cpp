#include "autoplay/policies/ClassicRulePilot.h"
#include "gameplay/GustPhysics.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// 经典模式规则策略实现。整体是"横向优先级瀑布 + 纵向滚动时域规划"：
//   1. 先定横向（决定本帧横移方向，纵向仿真沿该横向轨迹评估）；
//   2. 纵向：以 30Hz 仿真 2.4s 视界，评估一组闭环 mini-controller 候选
//      （滑翔 / 多相位跟踪 / 激进爬升），以"全程对垂直危险集的
//      最小间距"评分取优——振翅脉冲固定 62.5px 的相位问题由多相位候选 +
//      逐帧重规划共同化解（穿越间隙的时机对齐）；
//   3. 输出本帧动作，并附带音爆时机与应急护盾判定。
// 所有危险几何来自感知快照；仿真积分顺序与 PlayerAbilities::UpdateMotion
// 一致（重力 -> 振翅覆写 -> 位移含阵风四向漂移 -> 钳制），保证预测即所玩。
// =============================================================================

namespace {

// ===== 规划器参数（均为规划余量/近似系数，与玩法数值的对应关系见注释）=====
constexpr float kSimDt = 1.0f / 60.0f;      // 仿真步长（与游戏帧率同频，最小化触发离散化误差）
constexpr int   kSimSteps = 144;            // 视界 2.4s
constexpr float kBandMinX = 200.f;          // 默认驻留带（偏左留反应距离）
constexpr float kBandMaxX = 320.f;
constexpr float kBandDeadzone = 24.f;       // 驻留带死区（防抖）
constexpr float kPlanMargin = 6.f;          // 规划安全余量（碰撞体近似误差）
// ---- 管道碰撞体的真实几何（与 res/ResourceManager.h 的碰撞 UV 推导同源，改 UV 须同步）----
// 蝙蝠盒：UV(0.16,0.68)-(0.75,0.35) -> 半高≈0.165*scale（10.5px）、半宽≈0.30*scale（19px）；
// 管道三角在间隙沿的底边：UV x∈[0.281,0.62] -> 半宽≈0.34*halfWidth（21.7px）。
// 此前按视觉盒（半高 0.42*scale）规划会把 100px 间隙压成 46px 可悬停窗
// （< 振翅升量 62.5），约束无解导致投机轨迹——必须用真实几何。
constexpr float kBatHitHalfHFraction = 0.20f;  // 蝙蝠纵向碰撞半高系数（10.5px + 余量）
constexpr float kBatHitHalfWFraction = 0.30f;  // 蝙蝠横向碰撞半宽系数（≈19px）
constexpr float kPipeHitHalfWFraction = 0.34f; // 管道横向碰撞半宽 / halfWidth
constexpr float kBossSkillRFactor = 0.3f;   // 怪物技能命中半径系数（= batRadius*0.6*scale 推导）
constexpr float kBoltBatRadius = 26.f;      // 雷线判定蝙蝠等效半径（同 WeatherDirector 语义）
constexpr float kCorridorMargin = 10.f;     // 雷走廊规划余量
constexpr float kStormCorridorDrift = 60.f; // B5 未来雷漂移启发（折线总漂移 ≤120，取半程）
constexpr float kClimbRate = 105.f;         // 连锁振翅平均爬升率近似（62.5px/0.6s）
constexpr float kHoverFrac = 0.62f;         // 无管道时悬停高度（地面上方可玩高度比例）
constexpr float kBandKeepWeight = 4.f;      // 离带惩罚权重（视界内平均越带 px -> 分数）
constexpr float kBandHalfSlack = 8.f;       // 带半宽 = upMargin + 8（= gap/2 - 碰撞半高）
constexpr float kStormSpread = 140.f;       // B5 落点横向间距（预警期按当前位估算用）
constexpr float kFireAlignWindow = 130.f;   // 音爆开火的纵向对齐窗（本体纵向半径 150 内留余量）

// ===== 仿真状态（下坠冲刺取消后只剩高度/垂速/横位）=====
struct SimState {
    float y, vy, x;
};

// ===== 规划结果 =====
struct PlanResult {
    bool jumpNow = false;
    float minClearance = 1e9f;   // 视界内最小危险间距（负 = 侵入）
    float firstHitTime = -1.f;   // 首次侵入时刻（<0 = 视界内未侵入）
    float score = -1e9f;
};

enum class CandidateKind { Coast, Track, Climb };

// ---------- 基础查询 ----------

bool BossEngaged(const AutoPilotSenses& s) {
    return s.boss.present && s.boss.phase == EnemySensePhase::Engagement;
}

// 蝙蝠碰撞半高/半宽（真实 UV 几何 + 余量，见常量处推导）
float BatHitHalfH(const AutoPilotSenses& s) { return s.bat.scale.x * kBatHitHalfHFraction; }
float BatHitHalfW(const AutoPilotSenses& s) { return s.bat.scale.x * kBatHitHalfWFraction; }
// 管道横向碰撞重叠半窗（管道三角底边半宽 + 蝙蝠半宽）
float PipeOverlapX(const AutoPilotSenses& s, const PipeSense& p) {
    return p.halfWidth * kPipeHitHalfWFraction + BatHitHalfW(s);
}
// 怪物技能命中半径（OwlKing 以 ctx.batRadius*0.6 判定，batRadius=scale.x*0.5）
float SkillR(const AutoPilotSenses& s) { return s.bat.scale.x * kBossSkillRFactor; }

// 雷折线在高度 y 处的插值落点 x 与走廊半宽（折线自地面向天顶，与命中判定同源）
bool BoltAt(const LightningSense& l, float y, float& outX, float& outHalf) {
    const size_t n = l.boltPoints.size();
    if (n < 2 || l.boltWidths.size() < n) return false;
    auto corridorHalf = [&](size_t i) {
        return l.boltWidths[i] * 0.5f + kBoltBatRadius + kCorridorMargin;
    };
    if (y <= l.boltPoints.front().y) {
        outX = l.boltPoints.front().x;
        outHalf = corridorHalf(0);
        return true;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        const float y0 = l.boltPoints[i].y, y1 = l.boltPoints[i + 1].y;
        if (y >= y0 && y <= y1) {
            const float t = (y1 > y0) ? (y - y0) / (y1 - y0) : 0.f;
            outX = l.boltPoints[i].x + (l.boltPoints[i + 1].x - l.boltPoints[i].x) * t;
            outHalf = corridorHalf(i) + (corridorHalf(i + 1) - corridorHalf(i)) * t;
            return true;
        }
    }
    outX = l.boltPoints.back().x;   // 高于天顶：钳到顶节点
    outHalf = corridorHalf(n - 1);
    return true;
}

// 下一根未越过的管道（感知按 x 升序；px 为 t 秒后的列位置）
const PipeSense* NextPipe(const AutoPilotSenses& s, float batX, float t) {
    for (const auto& p : s.pipes) {
        const float px = p.x - s.worldSpeed * t;
        if (px + p.halfWidth > batX - 4.f) return &p;
    }
    return nullptr;
}

// 管道到达时刻的间隙中心：摆动外推 + 与 ObstacleField 同源的 clamp 公式
float PredictedGapCenter(const PipeSense& p, const AutoPilotSenses& s, float delay) {
    float c = p.baseGapCenter;
    if (p.oscAmp > 0.f && p.oscSpeed > 0.f)
        c += p.oscAmp * std::sin(p.oscSpeed * (s.oscClock + delay) + p.oscPhase);
    const float minC = s.safeMargin + p.gapSize * 0.5f + s.terrainY;
    const float maxC = s.screenH - s.safeMargin - p.gapSize * 0.5f;
    return std::clamp(c, minC, maxC);
}

// 仿真时刻的目标高度：下一根管道在到达时刻的间隙中心；无管道时悬停高度。
// upMargin 返回允许的目标上方越过量（间隙余量），供跟踪候选判断振翅时机。
float SimTarget(const AutoPilotSenses& s, float x, float t, int hDir, float* upMargin) {
    const PipeSense* p = NextPipe(s, x, t);
    if (!p) {
        if (upMargin) *upMargin = 40.f;
        return s.terrainY + (s.screenH - s.terrainY) * kHoverFrac;
    }
    float rel = s.worldSpeed - (float)hDir * s.hMoveSpeed; // 蝙蝠右移 = 相对靠近变慢
    const float px = p->x - s.worldSpeed * t;
    const float tc = (rel > 1.f) ? std::max(0.f, (px - x) / rel) : 5.f; // 顶墙/同速：视作远期
    if (upMargin) *upMargin = std::max(8.f, p->gapSize * 0.5f - BatHitHalfH(s) - 8.f);
    return PredictedGapCenter(*p, s, tc);
}

// ---------- 单步危险间距 ----------

// 仿真单步的有符号危险间距（负 = 已侵入）。pipeSoft 单独返回：段落接管期管道
// 不致死（ObstacleField 对怪物段落直接短路碰撞），只作舒适度低权重参与评分。
// iframe 期与游戏一致跳过全部碰撞判定（无敌短路）。
float StepClearance(const AutoPilotSenses& s, const SimState& st, float t,
                    bool iframe, float* pipeSoft) {
    *pipeSoft = 1e9f;
    if (iframe) return 1e9f;

    const float hitH = BatHitHalfH(s);
    float c = 1e9f;

    // 上下边缘（触边半高 + 规划余量；段落期弹心不即死，仍按硬危险回避）
    c = std::min(c, (st.y - s.edgeMargin - kPlanMargin) - s.terrainY);
    c = std::min(c, s.screenH - kPlanMargin - (st.y + s.edgeMargin));

    // 管道（横向碰撞重叠窗内取间隙上下沿间距；几何为真实 UV 碰撞体）
    float pipeC = 1e9f;
    for (const auto& p : s.pipes) {
        const float px = p.x - s.worldSpeed * t;
        if (std::fabs(px - st.x) >= PipeOverlapX(s, p)) continue;
        if (p.upSolid)   pipeC = std::min(pipeC, p.gapTop - (st.y + hitH));
        if (p.downSolid) pipeC = std::min(pipeC, (st.y - hitH) - p.gapBottom);
    }
    *pipeSoft = pipeC;
    if (!BossEngaged(s)) c = std::min(c, pipeC); // 段落期管道降为舒适度项

    // 雷线走廊（x 维）：预警倒计时到点后 ~1s 或危险窗内（折线即未来雷体）
    const LightningSense& L = s.lightning;
    const bool boltComing = L.warning && t >= L.warnRemaining && t <= L.warnRemaining + 1.0f;
    if (boltComing || L.dangerActive) {
        float bx = 0.f, half = 0.f;
        if (BoltAt(L, st.y, bx, half))
            c = std::min(c, std::fabs(st.x - bx) - half);
    }

    // 怪物威胁（仅交战期填充）
    if (BossEngaged(s)) {
        const EnemySense& b = s.boss;
        const float skillR = SkillR(s);
        // B2 血月凝视：全宽光带（预警起手即定；释放期缓慢扫掠由逐帧重规划跟随，
        // 沿扫掠方向固定前瞻 30px）
        if (b.skill == EnemySenseSkill::MoonGaze &&
            (b.skillState == EnemySenseSkillState::Telegraph ||
             b.skillState == EnemySenseSkillState::Active)) {
            c = std::min(c, std::fabs(st.y - (b.gazeY + b.gazeDir * 30.f)) -
                                (40.f + skillR + 15.f));
        }
        // B3 掠影穿屏：预警开始时锚定的蝙蝠高度带（离开该带即可）
        if (b.skill == EnemySenseSkill::ShadowDash &&
            (b.skillState == EnemySenseSkillState::Telegraph ||
             b.skillState == EnemySenseSkillState::Active)) {
            c = std::min(c, std::fabs(st.y - b.shadowBandY) - (70.f + skillR + 20.f));
        }
        // B5 雷暴三连：落点已知（Active 起有效），三走廊在释放窗内全程回避，
        // 漂移启发随高度放大（雷折线自落点向上漂移，低处走廊窄）
        if (b.stormArmed) {
            const float heightFrac = std::clamp(
                (st.y - s.terrainY) / std::max(1.f, s.screenH - s.terrainY), 0.f, 1.f);
            const float half = kBoltBatRadius + kCorridorMargin +
                               kStormCorridorDrift * heightFrac;
            for (int i = 0; i < 3; ++i)
                c = std::min(c, std::fabs(st.x - b.stormXs[i]) - half);
        }
        // B4 枭啸音波：扩张弧环（弧仅向弧心左半场扩散；缺口内穿越豁免）
        for (const auto& w : b.waves) {
            if (!w.alive) continue;
            const float dx = st.x - b.arcCenterX;
            if (dx >= 0.f) continue;
            const float r = w.radius + w.speed * t; // 半径随时间外推
            const float dy = st.y - w.centerY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float ringW = 14.f + skillR + 8.f;
            const float gapHalfW = w.gapHalf + skillR * 0.5f + 8.f;
            const float dR = std::fabs(dist - r);
            if (dR < ringW && std::fabs(st.y - w.gapY) > gapHalfW)
                c = std::min(c, -(ringW - dR)); // 处于弧环且不在缺口
        }
        // B1 羽刃 / B6 坠羽：点弹体按速度直线外推
        for (const auto& pr : b.projectiles) {
            if (!pr.alive) continue;
            const Vec2 fp = pr.hasVel ? Vec2(pr.pos.x + pr.vel.x * t, pr.pos.y + pr.vel.y * t)
                                      : pr.pos;
            const float R = 14.f + skillR + 8.f;
            const float dx = fp.x - st.x, dy = fp.y - st.y;
            c = std::min(c, std::sqrt(dx * dx + dy * dy) - R);
        }
    }
    return c;
}

// ---------- 候选评估 ----------

// 评估一个闭环候选：内置 mini-controller 驱动仿真积分并逐步评分。
// lead 为跟踪候选的振翅时机提前量（正 = 更早振翅，改变悬停振荡与穿越的相位）。
PlanResult Evaluate(const AutoPilotSenses& s, int hDir, CandidateKind kind, float lead) {
    SimState st{s.bat.pos.y, s.bat.vy, s.bat.pos.x};

    PlanResult r;
    int jumps = 0;
    float minPipeSoft = 1e9f;
    float bandOffsetSum = 0.f; // 离带守恒：管道之间的随意机动同样要付代价
    const float jumpGain = s.jumpStrength * s.jumpStrength / (2.f * s.gravity); // 振翅升量 62.5
    float t = 0.f;
    // 阵风四向漂移：换算系数与玩法侧同源；横向按生效剩余时间逐步累计位移，
    // 纵向作为速度直接叠加到每步位移（都只在生效窗口内起作用）。
    const float driftX = s.gust.active ? gust::DriftFromForce(s.gust.force.x) : 0.f;
    const float driftY = s.gust.active ? gust::DriftFromForce(s.gust.force.y) : 0.f;
    float driftXSum = 0.f;

    for (int step = 0; step < kSimSteps; ++step) {
        // ---- 本步目标高度（控制器决策与离带守恒共用同一来源）----
        float upMargin = 40.f;
        const float target = SimTarget(s, st.x, t, hDir, &upMargin);

        // ---- 内置控制器：本步是否振翅 ----
        // 预判式触发：用"下一步位置"判定——从振翅顶点回落到触发线时速度恰为
        // -jumpStrength，若按当前 y 判定会一步过冲 ~8px 击穿下沿（实测致命）
        bool jump = false;
        if (!s.bat.giant) {
            const bool wantUp = (st.vy <= 0.f && st.y < target);
            const float nextY = st.y + st.vy * kSimDt;
            switch (kind) {
                case CandidateKind::Coast:
                    break;
                case CandidateKind::Track:
                    if (wantUp && nextY <= target + upMargin - jumpGain + lead) jump = true;
                    break;
                case CandidateKind::Climb:
                    if (st.vy <= 50.f && st.y < target) jump = true;
                    break;
            }
        }
        if (step == 0) r.jumpNow = jump;

        // ---- 积分（与 UpdateMotion 同序：重力 -> 振翅覆写 -> 位移含漂移 -> 钳制）----
        const bool gustOn = s.gust.active && t < s.gust.remaining;
        st.vy += -s.gravity * kSimDt;
        if (jump) {
            st.vy = s.jumpStrength;
            ++jumps;
        }
        if (gustOn) driftXSum += driftX * kSimDt;
        st.y += (st.vy + (gustOn ? driftY : 0.f)) * kSimDt;
        st.y = std::clamp(st.y, s.terrainY + s.edgeMargin, s.screenH - s.edgeMargin);
        t += kSimDt;
        st.x = std::clamp(s.bat.pos.x + (float)hDir * s.hMoveSpeed * t + driftXSum,
                          s.xMin, s.xMax);

        // ---- 评分 ----
        // 不做无敌豁免：候选按真实弹道诚实评分，避免规划器投机于无敌帧
        float pipeSoft = 1e9f;
        const float ch = StepClearance(s, st, t, false, &pipeSoft);
        minPipeSoft = std::min(minPipeSoft, pipeSoft);
        if (ch < r.minClearance) {
            r.minClearance = ch;
        }
        if (ch < 0.f && r.firstHitTime < 0.f) r.firstHitTime = t;
        {
            const float bandHalf = upMargin + kBandHalfSlack; // 本步目标带的半宽
            bandOffsetSum += std::max(0.f, std::fabs(st.y - target) - bandHalf);
        }
    }

    // 视界内侵入硬危险：重罚并按侵入深度排序（少输当赢）；否则最大化最小间距，
    // 附：离带均值惩罚（管道间投机机动）+ 舒适度（段落期管道）+ 平滑偏好（少振翅）
    const float bandOffsetMean = bandOffsetSum / (float)kSimSteps;
    r.score = (r.minClearance < 0.f)
        ? -1000.f + r.minClearance * 10.f
        : r.minClearance + 0.3f * std::min(minPipeSoft, 30.f) - (float)jumps -
          kBandKeepWeight * bandOffsetMean;
    return r;
}

PlanResult PlanVertical(const AutoPilotSenses& s, int hDir) {
    PlanResult best;
    auto consider = [&](CandidateKind kind, float lead, float bias) {
        PlanResult r = Evaluate(s, hDir, kind, lead);
        r.score += bias;
        if (r.score > best.score) best = r;
    };
    // 多相位跟踪候选：振翅提前量 -40..+40 改变悬停振荡相对穿越时刻的相位
    // （薄余量穿越对相位敏感，5 档覆盖比 3 档显著降低全部候选同时违约的概率）
    consider(CandidateKind::Coast, 0.f, 0.5f);   // 同分稳定性：滑翔优先
    consider(CandidateKind::Track, -40.f, 0.f);
    consider(CandidateKind::Track, -20.f, 0.f);
    consider(CandidateKind::Track, 0.f, 0.25f);
    consider(CandidateKind::Track, 20.f, 0.f);
    consider(CandidateKind::Track, 40.f, 0.f);
    consider(CandidateKind::Climb, 0.f, 0.f);
    return best;
}

// ---------- 横向决策（威胁优先级瀑布）----------

int DecideHorizontal(const AutoPilotSenses& s) {
    const float x = s.bat.pos.x;
    const float roomL = x - s.xMin;
    const float roomR = s.xMax - x;
    const EnemySense& b = s.boss;

    // 1. B5 雷暴召唤：预警期即向墙壁余量大的方向冲刺（落点以蝙蝠位置为基准捕获，
    //    持续横移可让捕获点整体偏离；释放期继续脱离已知走廊）
    if (BossEngaged(s) && b.skill == EnemySenseSkill::StormCall &&
        (b.skillState == EnemySenseSkillState::Telegraph ||
         (b.skillState == EnemySenseSkillState::Active && b.stormArmed))) {
        return (roomR >= roomL) ? +1 : -1;
    }

    // 2. B6 血月坠羽：取标记列间 maximin 安全 x（活动边界与相邻列中点为候选），
    //    列 0.5s 换批，逐帧重评 + 死区防抖
    if (BossEngaged(s) && b.bloodColsActive) {
        float cols[4] = {b.bloodCols[0], b.bloodCols[1], b.bloodCols[2], b.bloodCols[3]};
        std::sort(cols, cols + 4);
        float bestX = x, bestD = -1.f;
        auto eval = [&](float cx) {
            float d = 1e9f;
            for (int i = 0; i < 4; ++i) d = std::min(d, std::fabs(cx - cols[i]));
            if (d > bestD) { bestD = d; bestX = cx; }
        };
        eval(s.xMin);
        eval(s.xMax);
        for (int i = 0; i + 1 < 4; ++i) eval((cols[i] + cols[i + 1]) * 0.5f);
        if (x < bestX - kBandDeadzone * 0.75f) return +1;
        if (x > bestX + kBandDeadzone * 0.75f) return -1;
        return 0;
    }

    // 3. 落雷走廊：预警期即逃离（折线已生成 = 未来雷体），危险窗内保持在外
    {
        float bx = 0.f, half = 0.f;
        const bool armed = (s.lightning.warning || s.lightning.dangerActive) &&
                           BoltAt(s.lightning, s.bat.pos.y, bx, half);
        if (armed && std::fabs(x - bx) < half + 20.f) {
            int dir = (bx >= x) ? -1 : +1; // 向远离雷线方向
            const float need = half + 30.f - std::fabs(x - bx);
            const float room = (dir > 0) ? roomR : roomL;
            const float roomOther = (dir > 0) ? roomL : roomR;
            if (room < need && roomOther > room) dir = -dir; // 该侧到不了墙就换侧
            return dir;
        }
    }

    // 4. 到达时间微调：下一根管道需要的垂直调整量超出名义到达时间内的可达量时，
    //    右移延后穿越买时间（爬升靠连锁振翅、下落靠重力，都需要时间）
    if (!BossEngaged(s)) {
        const PipeSense* p = NextPipe(s, x, 0.f);
        if (p) {
            const float arrival = std::max(0.05f, (p->x - x) / std::max(1.f, s.worldSpeed));
            const float target = PredictedGapCenter(*p, s, arrival);
            const float d = target - s.bat.pos.y; // >0 需爬升 / <0 需下落
            float need = 0.f;
            if (d > 40.f)
                need = d / kClimbRate + 0.25f; // 首跳延迟 + 平均爬升率
            else if (d < -60.f)
                need = std::sqrt(2.f * (-d) / s.gravity) * 1.15f; // 自由落体 + 余量
            if (need > arrival && roomR > 30.f) return +1;
        }
    }

    // 5. 默认驻留带（死区控制）
    if (x < kBandMinX) return +1;
    if (x > kBandMaxX) return -1;
    return 0;
}

// ---------- 攻击与应急 ----------

// 音爆时机：交战期充能就绪、处于技能间隔/力竭（避让优先）、与本体纵向对齐
// （波沿直线飞行，错位开火浪费充能）、且规划确认短期无硬危险。
bool ShouldFire(const AutoPilotSenses& s, const PlanResult& plan) {
    const EnemySense& b = s.boss;
    if (!BossEngaged(s) || b.sonicCharge <= 0) return false;
    if (b.skillState != EnemySenseSkillState::Idle &&
        b.skillState != EnemySenseSkillState::Fatigue) return false;
    if (std::fabs(s.bat.pos.y - b.bossY) > kFireAlignWindow) return false;
    return plan.minClearance > 30.f;
}

} // namespace

// =============================================================================
// 每帧决策入口
// =============================================================================

AutoPilotActions ClassicRulePilot::Decide(const AutoPilotSenses& s, float /*dt*/) const {
    AutoPilotActions a;

    // 横向先定（纵向仿真沿该横向轨迹评估威胁）
    a.horizontal = DecideHorizontal(s);

    // 纵向滚动时域规划
    const PlanResult plan = PlanVertical(s, a.horizontal);
    a.jump = plan.jumpNow;

    // 攻击（音爆）
    a.firePrimary = ShouldFire(s, plan);

    // 应急防护：两套技能独立判定，优先用能量制的无敌护盾（时长更长），
    // 无敌护盾不可用时退回一次性短护盾（CD 就绪即可，无能量门槛）
    const bool dangerSoon = plan.minClearance < 0.f && plan.firstHitTime >= 0.f &&
                            plan.firstHitTime <= 0.6f;
    if (!s.bat.invincible && dangerSoon) {
        if (s.bat.invincibleEnergy >= 1.0f) {
            a.releaseInvincible = true;
        } else if (s.bat.shortShieldReady) {
            a.releaseShortShield = true;
        }
    }
    return a;
}
