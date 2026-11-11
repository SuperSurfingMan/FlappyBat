#include "enemy/owlking/OwlKing.h"
#include "sprite/Sprite.h"
#include "render/Texture2D.h"
#include "render/SpriteRenderer.h"
#include "res/ResourceManager.h"
#include "core/Config.h"
#include "audio/AudioSystem.h"
#include "enemy/IStageServices.h"
#include "app/UnifiedSave.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

// =============================================================================
// D14 血月枭王 · EnemyDirector 框架实现（原 BossSystem 一期逻辑机械迁移，行为不变）
// 玩法设计：docs/BOSS系统设计文档.md（章节号见各处注释）
// 迁移对照：docs/怪物系统架构设计.md §8（事件队列 → ctx.fx 同步上行等映射）
// =============================================================================


// ---- 按模式调参（数值未变）----
// 第 2 期：按 ModeKey 查表，未登记的模式回落到明确命名的默认调参。
// 这是「怪物未针对该模式做定制」的设计语义，不是漏配兜底：新增游戏模式
// 因此完全不需要改动 OwlKing 或其它怪物代码（决策 D2 的隐藏成本被消除）。
const OwlKing::Tuning& OwlKing::TuningFor(ModeKey key) {
    static const Tuning kDefaultTuning =
        { 20, 75.f, 100, 150.f, 1.0f, { 60, 10, 40, 2, 20 } };
    static const std::unordered_map<std::string, Tuning> table = {
        { "classic",      { 20,     75.f, 100, 150.f, 1.0f, { 60, 10, 40, 2, 20 } } },
        { "time_rush",    { 999999, 30.f,  80,   0.f, 1.0f, { 40,  0, 40, 2, 20 } } },
        { "storm_night",  { 15,     45.f, 120,  90.f, 0.8f, { 40,  0, 60, 0,  0 } } },
    };
    auto it = table.find(std::string(key));
    return it != table.end() ? it->second : kDefaultTuning;
}

// ---- 全局攻击欲望（静态存储：枭王全局唯一，跨实例/跨局生效；语义见 OwlKing.h）----
float OwlKing::s_aggression = OwlKing::AggressionDefault;

void OwlKing::SetAggression(float a) {
    s_aggression = std::clamp(a, 0.f, 1.f);
}

float OwlKing::Aggression() {
    return std::clamp(s_aggression, 0.f, 1.f); // 存取双侧钳位：外部直改静态成员也安全
}

// ---- 形象资源批次（全局唯一；偏好持久化统一存档 O 标签行 "O <0|1>"）----
// 默认 legacy：存量玩家升级到本版本后视觉不变，矢量批次由设置页显式开启
static OwlKing::ArtBatch s_artBatch = OwlKing::ArtBatch::Legacy;

void OwlKing::LoadArtBatchPref() {
    for (const auto& line : UnifiedSave::ReadAllLines()) {
        if (line.rfind("O ", 0) != 0) continue;
        int v = -1;
        std::istringstream ss(line.substr(2));
        if (ss >> v && (v == 0 || v == 1)) {
            s_artBatch = (v == 1) ? ArtBatch::Vector : ArtBatch::Legacy;
        }
        return;
    }
}

void OwlKing::SetArtBatch(ArtBatch b) {
    if (s_artBatch == b) return;
    s_artBatch = b;
    UnifiedSave::RewriteSection({"O"}, {std::string("O ") + (b == ArtBatch::Vector ? "1" : "0")});
    std::cout << "[Boss] Art batch -> " << (b == ArtBatch::Vector ? "vector" : "legacy") << std::endl;
}

OwlKing::ArtBatch OwlKing::GetArtBatch() {
    return s_artBatch;
}

// 批次感知纹理键：Vector 批次与 legacy 同名表一一对应（ResourceManager "_vec" 后缀注册）
std::string OwlKing::ArtKey(const char* legacyKey) {
    return (s_artBatch == ArtBatch::Vector) ? std::string(legacyKey) + "_vec" : std::string(legacyKey);
}

void OwlKing::Init(const Config& cfg, ResourceManager& res) {
    cfg_ = &cfg;
    res_ = &res;
    auto& rm = res;
    LoadArtBatchPref(); // 进入游戏前读批次偏好（静态开关，设置页可再切）

    // 枭王本体：初始为拍翼循环（透明底 2x5 网格 10 帧；后续由动画状态机按状态切表）
    // 纹理键经 ArtKey 按批次解析（legacy 原键 / vector "_vec" 后缀键，网格参数共用）
    owlSprite = std::make_shared<Sprite>(rm.GetTexture2D(ArtKey("bossOwlFlapSheet10")));
    ApplyClip(owlSprite, Anim::Flap);
    curAnim = Anim::Flap;
    owlSprite->SetPosition(BossPosX, 480.f);

    // 血月（Warning 期起右上角淡入）
    moonSprite = std::make_shared<Sprite>(rm.GetTexture2D("bloodMoon"));
    moonSprite->SetLumaKey(true, 0.06f, 0.12f);
    moonSprite->SetScale(200.f, 200.f);
    moonSprite->SetPosition(0.f, 0.f); // 位置在 RenderWorld 中按窗口尺寸布置

    // B3 掠影专用精灵（ShadowDash Active 起始时经 ApplyClip 切表起播）
    dashSprite = std::make_shared<Sprite>(rm.GetTexture2D(ArtKey("bossOwlDashSheet")));

    // 羽刃对象池（B1 弹体）
    auto featherTex = rm.GetTexture2D("featherBlade");
    feathers.Init(8, [&] {
        auto s = std::make_shared<Sprite>(featherTex);
        s->SetLumaKey(true, 0.06f, 0.12f);
        s->SetScale(18.f, 34.f);
        return s;
    });

    // 音爆对象池（2x2 网格 4 帧脉冲）
    auto waveTex = rm.GetTexture2D("sonicWaveSheet");
    waves.Init(3, [&] {
        auto s = std::make_shared<Sprite>(waveTex);
        s->InitAnimationByTexture(waveTex, 2, 2);
        s->GetAnimation()->SetFrameTime(0.15f);
        s->GetAnimation()->SetLoop(false); // 一次脉冲后保持末帧
        s->SetLumaKey(true, 0.06f, 0.12f);
        s->SetScale(34.f, 24.f);
        return s;
    });

    // B6 坠羽对象池（复用羽刃贴图，渲染红 tint + 羽尖朝下）
    bloodDrops.resize(40); // 稳态并发 ≈ 4 列 x 2.2s 存活 / 0.3s 生成间隔 ≈ 30，留余量防生成被丢弃
    bloodSprites.reserve(bloodDrops.size());
    for (size_t i = 0; i < bloodDrops.size(); ++i) {
        auto s = std::make_shared<Sprite>(featherTex);
        s->SetLumaKey(true, 0.06f, 0.12f);
        s->SetScale(16.f, 30.f);
        bloodSprites.push_back(s);
    }

    // 回声水晶对象池（充能道具）
    auto crystalTex = rm.GetTexture2D("echoCrystal");
    crystals.Init(CrystalMax, [&] {
        auto s = std::make_shared<Sprite>(crystalTex);
        s->SetLumaKey(true, 0.06f, 0.12f);
        s->SetScale(36.f, 36.f);
        return s;
    });
}

void OwlKing::OnRunStart() {
    EnterHidden(false);
    everSpawned = false;
    spawnCount = 0;
    sonicCharge = 0; // 声波槽跨「遭遇」保留、跨「局」清零（§4.2）
    pendingKill_ = false;
}

void OwlKing::OnBatDead() {
    // 蝙蝠死亡/中途退出：中止战斗，再降临计时从头开始
    EnterHidden(false);
}

// ===== 阶段切换（§3 状态机） =====

void OwlKing::EnterWarning(const EnemyContext& ctx) {
    phase = Phase::Warning;
    phaseTimer = 0.f;
    everSpawned = true;
    spawnCount++;
    // 再降临 HP 递增（经典 ×1.5/次，上限 400；一场内多次降临的泛化规则）
    const Tuning& t = TuningFor(ctx.mode);
    float mult = 1.f + 0.5f * (float)(spawnCount - 1);
    hpMax = (int)std::min((float)t.hpBase * mult, 400.f);
    hp = hpMax;
    combatHits = 0;
    batHearts = BatHeartsMax;
    lastStage = 1;        // 满血起步（阶段跃迁检测基线）
    barShakeTimer = 0.f;
    crystalTimer = 0.f;
    std::cout << "[Boss] Warning started (spawn #" << spawnCount
              << ", hp " << hpMax << ")" << std::endl;
    ctx.fxFx->PostWeatherMsg(WeatherSlot(), "夜枭将至", 2.0f, false); // 5s 预警（闪烁，提前量同场景预警惯例）
    ctx.fxFx->PlaySFX((int)SFX::Thunder, 0.5f); // §3/§8 枭鸣前奏（一期复用雷音低音量）
}

void OwlKing::EnterEngagement(const EnemyContext& ctx) {
    phase = Phase::Engagement;
    phaseTimer = 0.f;
    batHearts = BatHeartsMax;
    sonicCharge = std::max(sonicCharge, 1); // 遭遇战开场保证 1 段（§4.2）
    skillState = SkillState::Idle;
    intervalTimer = 1.5f;                  // 现身淡入后稍等即出首个技能（入场节拍，刻意不随攻击欲望缩放）
    curSkill = Skill::None;
    lastSkill = Skill::None;
    std::cout << "[Boss] Engagement started." << std::endl;
    ctx.fxFx->PostWeatherMsg(WeatherSlot(), "血月枭王", 0.8f, true); // 闪烁后转常驻状态行
}

void OwlKing::EnterDefeat() {
    phase = Phase::Defeat;
    phaseTimer = 0.f;
    // 死亡消散动画由 ApplyAnimation 统一切表（本帧 Update 末尾生效）
    // 清场：技能与弹幕即刻消失
    for (auto& f : feathers.items) f.alive = false;
    for (auto& w : waves.items) w.alive = false;
    for (auto& c : crystals.items) c.alive = false;
    for (auto& sc : screeches) sc.alive = false;
    for (auto& d : bloodDrops) d.alive = false;
    curSkill = Skill::None;
    skillState = SkillState::Idle;

    // 击杀奖励（原场景 Defeated 事件分支迁入；§7.2 按模式差异化，数值取 TuningFor::Rewards）
    const Tuning::Rewards& rw = TuningFor(ctx_->mode).rewards;
    const int scoreBonus = rw.scoreBase + rw.scorePerHeart * batHearts;
    const int coinBonus = rw.coinBase + std::min(combatHits * rw.coinPerHit, rw.coinHitCap);
    IFeedbackFx& fx = *ctx_->fxFx;
    IRunEconomy& eco = *ctx_->fxEconomy;
    IPlayerCommands& pl = *ctx_->fxPlayer;
    eco.AddScore((size_t)scoreBonus);
    eco.AddCoins(coinBonus);
    eco.AddTimeLimit(8.0f); // 限时模式 +8s（非限时为空操作）
    fx.RemoveWeatherMsg(WeatherSlot());
    fx.FloatTextAt("枭王陨落！", Vec2{ctx_->screenW * 0.5f, ctx_->screenH - 220.f},
                 UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 30.f, 2.5f);
    fx.FloatTextAt("+" + std::to_string(scoreBonus) + " 分  +" + std::to_string(coinBonus) + " 金币",
                 Vec2{ctx_->screenW * 0.5f, ctx_->screenH - 190.f},
                 0.95f, 0.82f, 0.35f, 20.f, 2.5f);
    fx.PlaySFX((int)SFX::Record, 1.0f); // 击杀号角（一期复用破纪录音效）
    fx.Shake(14.0f);                   // §8 抖动表：击杀 14
    pl.TriggerSlowMo(KillSlowMoDur);   // §10.5 击杀慢动作（C12 联动；场景侧线性缓升恢复）
    // 成就（§7.2 三期）：弑枭者（累计击杀 1/5/15）/ 无伤弑枭（满心击杀累计）
    eco.ReportAchievement("boss_kill", 1);
    if (batHearts >= BatHeartsMax) {
        eco.ReportAchievement("boss_flawless", 1);
    }
    std::cout << "[Boss] Defeated! hearts left " << batHearts
              << ", hits " << combatHits << std::endl;
}

void OwlKing::EnterLeave() {
    phase = Phase::Leave;
    phaseTimer = 0.f;
    for (auto& f : feathers.items) f.alive = false;
    for (auto& w : waves.items) w.alive = false;
    for (auto& c : crystals.items) c.alive = false;
    for (auto& sc : screeches) sc.alive = false;
    for (auto& d : bloodDrops) d.alive = false;
    curSkill = Skill::None;
    skillState = SkillState::Idle;
    IFeedbackFx& fx = *ctx_->fxFx;
    fx.RemoveWeatherMsg(WeatherSlot());
    fx.PostWeatherMsg(WeatherSlot(), "枭王隐去", 1.5f, false);
    std::cout << "[Boss] Left (timeout)." << std::endl;
}

void OwlKing::EnterHidden(bool /*returning*/) {
    phase = Phase::Hidden;
    phaseTimer = 0.f;
    hiddenTimer = 0.f;
    returnTimer = 0.f;
    combatHits = 0;
    batHearts = BatHeartsMax;
    pendingKill_ = false;
    flinchTimer = 0.f;      // 受击表演随段落中止一并复位
    fatigueOutTimer = 0.f;  // 力竭展开过渡同理
    heartBreakTimer = 0.f;  // 心形碎裂残影同理
    heartBreakIdx = -1;
    // 隐匿即清空本怪物全部消息行（常驻/预警/技能闪现），覆盖一切未显式移除的路径：
    // 「枭王隐去」播报随离场结束消失、预警/交战中蝙蝠死亡不再残留「夜枭将至」「血月枭王」
    if (lastFxFx_) {
        lastFxFx_->RemoveWeatherMsg(WeatherSlot());
        lastFxFx_->RemoveWeatherMsg(SkillMsgSlot());
    }
    // 拍翼循环由 ApplyAnimation 在下帧 Update 统一切回（此处本体不可见，无可见跳变）
    for (auto& f : feathers.items) f.alive = false;
    for (auto& w : waves.items) w.alive = false;
    for (auto& c : crystals.items) c.alive = false;
    for (auto& sc : screeches) sc.alive = false;
    for (auto& d : bloodDrops) d.alive = false;
}

// ===== 主更新 =====

void OwlKing::Update(float dt, const EnemyContext& ctx) {
    ctx_ = &ctx;
    lastFxFx_ = ctx.fxFx; // 供 Update 之外的生命周期方法（如 EnterHidden 清消息行）使用

    // 输入意图：音爆冲击波（原场景路由；此刻状态=上一帧末，时序与原实现一致）
    if (ctx.firePrimary && TryFireSonic(ctx.batPos)) {
        ctx.fxFx->PlaySFX((int)SFX::Thunder, 0.4f); // 发射反馈（一期复用雷音低音量）
        ctx.fxPlayer->BounceBat(-SonicRecoil, 0.f);     // 发射后坐：向左位移回弹（§4.3）
    }

    worldClock += dt;
    if (hitFlashTimer > 0.f) hitFlashTimer -= dt;
    if (hitInvTimer > 0.f) hitInvTimer -= dt;
    if (sonicCdTimer > 0.f) sonicCdTimer -= dt;
    if (barShakeTimer > 0.f) barShakeTimer -= dt;
    if (flinchTimer > 0.f) flinchTimer -= dt;
    if (fatigueOutTimer > 0.f) fatigueOutTimer -= dt;
    if (heartBreakTimer > 0.f) heartBreakTimer -= dt;

    switch (phase) {
        case Phase::Hidden: {
            hiddenTimer += dt;
            returnTimer += dt; // 再降临间隔：自上次 Defeat/Leave 结束（EnterHidden）起累计
            // 降临门槛（§3 触发条件）：限时=剩余时间阈值；否则 分数/开局秒数先到者优先
            const Tuning& t = TuningFor(ctx.mode);
            bool gate = (ctx.timeLeft >= 0.f)
                ? (ctx.timeLeft <= t.timeGate)
                : ((int)ctx.score >= t.scoreGate || hiddenTimer >= t.timeGate);
            // 再降临需击败后间隔（限时模式击败后本局不再降临）
            bool comebackOK = !everSpawned ||
                (t.returnSec > 0.f && returnTimer >= t.returnSec);
            if (gate && comebackOK) EnterWarning(ctx);
            break;
        }
        case Phase::Warning:
            phaseTimer += dt;
            UpdateCrystals(dt, ctx); // 预警期即开始生成水晶（§3）
            if (phaseTimer >= WarningDur) EnterEngagement(ctx);
            break;
        case Phase::Engagement:
            phaseTimer += dt;
            UpdateEngagement(dt, ctx);
            break;
        case Phase::Defeat:
            phaseTimer += dt;
            if (phaseTimer >= DefeatDur) EnterHidden(true);
            break;
        case Phase::Leave:
            phaseTimer += dt;
            if (phaseTimer >= LeaveDur) EnterHidden(true);
            break;
    }

    // 表演层：按状态机推导期望动画并按需切表，再推进帧
    ApplyAnimation();
    owlSprite->Update(dt);
    dashSprite->Update(dt);
    for (auto& s : waves.sprites) s->Update(dt);

    // 心数归零：状态机推进完成后统一致死（对齐原事件出队时序，避免帧内重入复位）
    if (pendingKill_) {
        pendingKill_ = false;
        ctx.fxPlayer->KillPlayer("owlking");
    }
    ctx_ = nullptr;
}


void OwlKing::UpdateEngagement(float dt, const EnemyContext& ctx) {
    // 纵向浮游：慢漂 + 小幅摆动，钳制在 200~760（§2）
    bossY = 480.f + 220.f * std::sin(worldClock * 0.57f)
                 + 60.f  * std::sin(worldClock * 1.05f);
    bossY = std::clamp(bossY, 200.f, 760.f);

    UpdateSkill(dt, ctx);
    UpdateProjectiles(dt, ctx);
    UpdateCrystals(dt, ctx);

    // 撞地/撞顶：遭遇期不即死，扣 1 心并弹起（§6 受击模型）
    if (ctx.batPos.y - 10.f <= ctx.terrainY) {
        DamageBat(ctx, -1.f);
    } else if (ctx.batPos.y + 10.f >= ctx.screenH) {
        DamageBat(ctx, +1.f);
    }

    // 交战超时离场（限时模式由局内倒计时自然结算，不设超时）
    if (ctx.timeLeft < 0.f && phaseTimer >= EngageTimeout) {
        EnterLeave();
    }
}

// ===== 技能循环：间隔 → 预警 → 释放 → 力竭（§3 交战期循环） =====

int OwlKing::Stage() const {
    float frac = hpMax > 0 ? (float)hp / (float)hpMax : 0.f;
    if (frac > 0.60f) return 1;
    if (frac > 0.25f) return 2;
    return 3;
}

float OwlKing::SkillInterval() const {
    // 三阶段递缩（§5 技能循环规则）x 攻击欲望全局缩放（只改释放频率，不改技能池与各技能时长）
    float base;
    switch (Stage()) {
        case 1:  base = 4.0f + 2.0f * Random::Get().GetRandomFloat(); break;
        case 2:  base = 3.0f + 1.5f * Random::Get().GetRandomFloat(); break;
        default: base = 2.5f + 1.0f * Random::Get().GetRandomFloat(); break;
    }
    return base * IntervalFactor();
}

// 攻击欲望 → 技能间隔缩放：4^(1-2a) 指数曲线，对称于中值——
// 0.5=×1.0（现行节奏）、1.0=×0.25、0.0=×4.0；与风暴之夜 cdScale（衰减端 ×0.8）正交叠加
float OwlKing::IntervalFactor() const {
    return std::pow(4.f, 1.f - 2.f * Aggression());
}

float OwlKing::DamageMult() const {
    return (skillState == SkillState::Fatigue) ? 1.5f : 1.0f;
}


// B6：重掷顶部标记列（全玩法区随机，列宽 60；两两最小间距 90，防标记列重叠）
void OwlKing::RollBloodCols(const EnemyContext& ctx) {
    const float lo = 60.f;
    const float hi = std::max(lo + BloodColW, ctx.screenW - 60.f - BloodColW);
    for (size_t i = 0; i < bloodCols.size(); ++i) {
        float x = lo;
        for (int tries = 0; tries < 12; ++tries) {
            x = lo + (hi - lo) * Random::Get().GetRandomFloat();
            bool ok = true;
            for (size_t j = 0; j < i; ++j) {
                if (std::fabs(x - bloodCols[j]) < BloodColMinGap) { ok = false; break; }
            }
            if (ok) break;
        }
        bloodCols[i] = x;
    }
}

// ===== 段落规则声明（遭遇期接管世界规则；见玩法设计文档 §3/§9） =====

EncounterRules OwlKing::ActiveRules() const {
    if (phase != Phase::Engagement) return EncounterRules{};
    EncounterRules r;
    r.suppressPipeSpawn = true;    // 停止新管道列，让场上管道自然滚出（视野聚焦 BOSS 战）
    r.freezeOffscreenPipes = true; // 池内尚未入画的管道一并冻结，不再从右缘滚入
    r.suppressItemSpawn = true;    // 走廊道具停生成（回声水晶是唯一道具）
    r.pipesLethal = false;         // 管道不即死（3 心模型）
    r.edgesLethal = false;         // 撞地/顶弹起扣心，交 UpdateEngagement 处理
    r.pauseGust = true;            // 阵风暂停（§9：避免与技能双重位移压力；二期启用）
    r.scrimBoost = 0.10f;          // 天色额外压暗（§9 +0.10；二期启用）
    r.minRainIntensity = 0.80f;    // 雨强下限抬到 ≥80%（§9：血月雨夜意象；结束回落档位基线）
    return r;
}
