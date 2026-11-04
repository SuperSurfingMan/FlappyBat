#include "enemy/owlking/OwlKing.h"
#include "sprite/Sprite.h"
#include "render/Texture2D.h"
#include "render/SpriteRenderer.h"
#include "res/ResourceManager.h"
#include "core/Config.h"
#include "audio/AudioSystem.h"
#include "enemy/IStageFeedback.h"
#include "app/UnifiedSave.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

// =============================================================================
// D14 血月枭王 · EnemyDirector 框架实现（原 BossSystem 一期逻辑机械迁移，行为不变）
// 玩法设计：docs/BOSS系统设计文档.md（章节号见各处注释）
// 迁移对照：docs/怪物系统架构设计.md §8（事件队列 → ctx.fx 同步上行等映射）
// =============================================================================

// ---- 序列帧帧序工具 ----
// 加载器垂直翻转（stbi flipY）使网格切片 r0 对应画面底行；旋转 (rows-1)*cols 使
// 「画面顶行优先」——死亡消散（顶行=完整→崩解，底行=漩涡→消散）的播放方向才正确；
// 拍翼为闭环循环，重排仅对齐作画相位。单行表不旋转（天然从左到右）。
// UV 内缩 1.5 纹素：线性采样下杜绝邻格渗色（关键帧边缘出现其它关键帧的边缘图像）。
static std::vector<FrameUV> TopRowFirstFrames(const std::shared_ptr<Texture2D>& tex, int rows, int cols) {
    std::vector<FrameUV> frames = Texture2D::ClipFramesFromSpriteSheet(rows, cols);
    const float du = 1.5f / (float)tex->GetWidth();
    const float dv = 1.5f / (float)tex->GetHeight();
    for (auto& f : frames) {
        f.uvMin.x += du;
        f.uvMin.y += dv;
        f.uvMax.x -= du;
        f.uvMax.y -= dv;
    }
    if (rows > 1)
        std::rotate(frames.begin(), frames.begin() + (rows - 1) * cols, frames.end());
    return frames;
}

// ---- 按模式调参（原 MainScene::ParamsOf 表的 boss 五列 + 击杀奖励 §7.2；数值未变） ----
const OwlKing::Tuning& OwlKing::TuningFor(GameMode mode) {
    static const Tuning table[] = {
        /* Classic    */ { 20,     75.f, 100, 150.f, 1.0f, { 60, 10, 40, 2, 20 } },
        /* TimeRush   */ { 999999, 30.f,  80,   0.f, 1.0f, { 40,  0, 40, 2, 20 } }, // 高分门槛=永不按分降临；剩 30s 触发；不再降临
        /* StormNight */ { 15,     45.f, 120,  90.f, 0.8f, { 40,  0, 60, 0,  0 } }, // 得分 ×2 由 AddScore 模式倍率兑现（80 实得）
    };
    return table[(int)mode];
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

void OwlKing::Init() {
    auto& rm = ResourceManager::Get();
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
    ctx.fx->PostWeatherMsg(WeatherSlot(), "夜枭将至", 2.0f, false); // 5s 预警（闪烁，提前量同场景预警惯例）
    ctx.fx->PlaySFX((int)SFX::Thunder, 0.5f); // §3/§8 枭鸣前奏（一期复用雷音低音量）
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
    ctx.fx->PostWeatherMsg(WeatherSlot(), "血月枭王", 0.8f, true); // 闪烁后转常驻状态行
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
    IStageFeedback& fx = *ctx_->fx;
    fx.AddScore((size_t)scoreBonus);
    fx.AddCoins(coinBonus);
    fx.AddTimeLimit(8.0f); // 限时模式 +8s（非限时为空操作）
    fx.RemoveWeatherMsg(WeatherSlot());
    fx.FloatTextAt("枭王陨落！", Vec2{ctx_->screenW * 0.5f, ctx_->screenH - 220.f},
                 UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 30.f, 2.5f);
    fx.FloatTextAt("+" + std::to_string(scoreBonus) + " 分  +" + std::to_string(coinBonus) + " 金币",
                 Vec2{ctx_->screenW * 0.5f, ctx_->screenH - 190.f},
                 0.95f, 0.82f, 0.35f, 20.f, 2.5f);
    fx.PlaySFX((int)SFX::Record, 1.0f); // 击杀号角（一期复用破纪录音效）
    fx.Shake(14.0f);                   // §8 抖动表：击杀 14
    fx.TriggerSlowMo(KillSlowMoDur);   // §10.5 击杀慢动作（C12 联动；场景侧线性缓升恢复）
    // 成就（§7.2 三期）：弑枭者（累计击杀 1/5/15）/ 无伤弑枭（满心击杀累计）
    fx.ReportAchievement(AchievementEvent::BossKill, 1);
    if (batHearts >= BatHeartsMax) {
        fx.ReportAchievement(AchievementEvent::BossFlawless, 1);
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
    IStageFeedback& fx = *ctx_->fx;
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
    if (lastFx_) {
        lastFx_->RemoveWeatherMsg(WeatherSlot());
        lastFx_->RemoveWeatherMsg(SkillMsgSlot());
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
    lastFx_ = ctx.fx; // 供 Update 之外的生命周期方法（如 EnterHidden 清消息行）使用

    // 输入意图：音爆冲击波（原场景路由；此刻状态=上一帧末，时序与原实现一致）
    if (ctx.firePrimary && TryFireSonic(ctx.batPos)) {
        ctx.fx->PlaySFX((int)SFX::Thunder, 0.4f); // 发射反馈（一期复用雷音低音量）
        ctx.fx->BounceBat(-SonicRecoil, 0.f);     // 发射后坐：向左位移回弹（§4.3）
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
        ctx.fx->KillPlayer("owlking");
    }
    ctx_ = nullptr;
}

// ===== 表演层：本体动画状态机（docs/BOSS美术素材生成方案 §七）=====

void OwlKing::ApplyAnimation() {
    const Anim want = DesiredAnim();
    const ArtBatch batch = GetArtBatch();
    if (want == curAnim && batch == appliedBatch) return;
    ApplyClip(owlSprite, want); // Swoop 入离场均循环（持续飞行节拍，避免定格悬停的僵直感）
    // 设置页切换批次后同动画也在此重挂（appliedBatch 失配触发，切表即 Reset 从头播）
    curAnim = want;
    appliedBatch = batch;
}

OwlKing::Anim OwlKing::DesiredAnim() const {
    if (phase == Phase::Defeat) return Anim::Death;   // 死亡消散（非循环，播完转 Hidden）
    if (phase == Phase::Leave) return Anim::Swoop;    // 离场掠出（循环振翅）
    if (flinchTimer > 0.f) return Anim::Flinch;       // 受击缩颈（短暂覆盖，结束回落期望态）
    if (phase == Phase::Engagement && phaseTimer < EngageFadeIn)
        return Anim::Swoop;                           // 降临俯冲入场（0.8s 与雾化淡入同步）
    if (skillState == SkillState::Telegraph || skillState == SkillState::Active) {
        if (curSkill == Skill::ScreechWave) return Anim::Screech;   // B4 喙大张贯穿预警与释放
        if (curSkill == Skill::StormCall) return Anim::StormCall;   // B5 抬翼指天定格
        if (curSkill == Skill::ShadowDash && skillState == SkillState::Active)
            return Anim::Flap;                       // 本体隐身（掠影走 dashSprite，本体动画不外露）
        return Anim::Cast;                           // B1/B2/B6：蓄力姿势自预警保持到释放
    }
    if (skillState == SkillState::Fatigue) return Anim::Fatigue;  // 力竭垂翼（半透明已有）
    if (fatigueOutTimer > 0.f) return Anim::FatigueOut; // 收势反向舒展：末帧≈张翼，衔接下一动画首帧
    if (Stage() >= 3) return Anim::Rage;              // 三阶段狂暴 idle（羽毛炸起+红纹）
    return Anim::Flap;
}

void OwlKing::ApplyClip(const std::shared_ptr<Sprite>& s, Anim a, int loopOverride) {
    // 本体动画表（渲染尺寸与帧时间的唯一事实来源；素材网格见 docs/BOSS美术素材生成方案 §四）。
    // 素材已经 tools/normalize_owl_sheets.py 归一：全部 640 方格、红瞳锚点=格正中、脸宽统一定标，
    // 因此所有动画共用 kBodyQuad 方形渲染即天然体型/位置一致；fit 仅区分本体（1）与掠影（2）。
    struct ClipSpec { const char* key; int rows, cols; float frameTime; bool loop; int fit; bool rev; };
    static const ClipSpec kClips[] = {
        /* Flap      */ { "bossOwlFlapSheet10",    2, 5, 0.10f,  true,  1, false },
        /* Death     */ { "bossDeathSheet10",      2, 5, 0.20f,  false, 1, false }, // 10 帧 x 0.2 = DefeatDur
        /* Cast      */ { "bossOwlCastSheet",      2, 3, 0.15f,  false, 1, false }, // 0.9s 后定格末帧
        /* Fatigue   */ { "bossOwlFatigueSheet",   1, 4, 0.30f,  false, 1, false },
        /* FatigueOut*/ { "bossOwlFatigueSheet",   1, 4, 0.10f,  false, 1, true  }, // 同表倒放：收势→舒展，衔接下一动画首帧
        /* Rage      */ { "bossOwlRageSheet",      2, 3, 0.07f,  true,  1, false },
        /* Flinch    */ { "bossOwlHitSheet",       1, 4, 0.10f,  false, 1, false }, // 0.4s：过短读作抽搐
        /* Screech   */ { "bossOwlScreechSheet",   2, 3, 0.15f,  true,  1, false },
        /* StormCall */ { "bossOwlStormCallSheet", 1, 4, 0.15f,  false, 1, false },
        /* Dash      */ { "bossOwlDashSheet",      2, 3, 0.07f,  false, 2, false }, // 6 帧 ≈ ShadowDashDur
        /* Swoop     */ { "bossSwoopSheet",        1, 4, 0.20f,  true,  1, false }, // 飞行节拍循环（入离场共用）
    };
    const ClipSpec& c = kClips[(int)a];
    auto tex = ResourceManager::Get().GetTexture2D(ArtKey(c.key)); // 批次感知键（legacy/vector 同构表）
    // 注意：Sprite 的 Animation 为懒创建（仅 InitAnimation* 内部 make_unique），
    // 必须先经 Sprite 层 Init 建立对象，GetAnimation() 才非空
    s->InitAnimationByTexture(tex, (size_t)c.rows, (size_t)c.cols);
    std::vector<FrameUV> framesUV = TopRowFirstFrames(tex, c.rows, c.cols); // 顶行优先帧序 + UV 内缩
    if (c.rev)
        std::reverse(framesUV.begin(), framesUV.end()); // 倒放（FatigueOut：收势反向舒展）
    s->GetAnimation()->InitAnimationUV(tex, framesUV);
    s->GetAnimation()->SetFrameTime(c.frameTime);
    s->GetAnimation()->SetLoop(loopOverride < 0 ? c.loop : loopOverride != 0);
    s->GetAnimation()->Reset();
    if (c.fit == 2)
        s->SetScale(DashGhostW, DashGhostW);
    else
        s->SetScale(kBodyQuad, kBodyQuad);
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

void OwlKing::StartSkill(Skill s, const EnemyContext& ctx) {
    curSkill = s;
    lastSkill = s;
    skillState = SkillState::Telegraph;
    skillTimer = 0.f;
    if (s == Skill::MoonGaze) {
        // 抽取光带位置与扫掠方向（§5 B2）
        float lo = std::max(240.f, 40.f + 80.f);
        float hi = std::min(720.f, ctx.screenH - 60.f);
        gazeY = lo + (hi - lo) * Random::Get().GetRandomFloat();
        gazeDir = Random::Get().GetRandomFloat() < 0.5f ? -1.f : 1.f;
    }
    if (s == Skill::ShadowDash) {
        shadowBandY = ctx.batPos.y; // 暗影带锚定蝙蝠当前高度（§5 B3：离开该高度带即可躲避）
    }
    if (s == Skill::ScreechWave) {
        // 三道音波的缺口位置起手即定（随机高度、逐道错位 ≥150），波体按 0.6s 间隔在 Active 内依次发出
        float prev = -1e9f;
        const float lo = ctx.terrainY + 110.f;
        const float hi = std::max(lo + 30.f, ctx.screenH - 120.f);
        for (auto& w : screeches) {
            w.alive = false;
            w.age = 0.f;
            w.centerY = bossY;
            float g = lo;
            for (int tries = 0; tries < 8; ++tries) {
                g = lo + (hi - lo) * Random::Get().GetRandomFloat();
                if (std::fabs(g - prev) >= 150.f) break;
            }
            w.gapY = g;
            prev = g;
        }
        screechSpawned = 0;
    }
    if (s == Skill::StormCall) {
        stormStruck = 0; // 三落点在 Active 开始时按蝙蝠位置捕获（§5 B5）
    }
    if (s == Skill::BloodRain) {
        RollBloodCols(ctx); // 顶部标记列（预警期先亮出第一批，0.5s 换批）
        bloodReroll = BloodRerollCd;
        bloodSpawn = 0.f;
    }
    // 技能蓄力提示音（一期复用 Countdown）
    ctx.fx->PlaySFX((int)SFX::Countdown, 0.8f);
    // 消息盒技能预警（§5：B1「羽刃来袭」/ B4「枭啸蓄力」；独立槽位避免顶掉常驻枭王行）
    if (s == Skill::FeatherStorm) ctx.fx->PostWeatherMsg(SkillMsgSlot(), "羽刃来袭", 0.8f, false);
    if (s == Skill::ScreechWave) ctx.fx->PostWeatherMsg(SkillMsgSlot(), "枭啸蓄力", 1.0f, false);
}

void OwlKing::UpdateSkill(float dt, const EnemyContext& ctx) {
    switch (skillState) {
        case SkillState::Idle: {
            intervalTimer -= dt * TuningFor(ctx.mode).cdScale; // cdScale：风暴之夜 0.8（技能更密）
            // 三阶段狂暴（§3）：体表泛红光脉动（弹速 +15% 与技能间隔缩短已在各自数值处生效）
            if (Stage() >= 3 && hitFlashTimer <= 0.f)
                owlSprite->SetGlow(0.35f + 0.25f * (0.5f + 0.5f * std::sin(worldClock * 6.f)));
            if (intervalTimer <= 0.f) {
                // 技能池（§5）：一阶段 {B1,B2}；二阶段（HP≤60%）解锁 B3/B4；
                // 三阶段（HP≤25%）解锁 B5 雷暴召唤 / B6 血月坠羽；不连续重复
                Skill pick;
                if (Stage() >= 3 && TestForceBloodRain) {
                    // 测试开关：三阶段必定释放血月坠羽（绕过不重复规则，便于连续验证 B6）
                    pick = Skill::BloodRain;
                } else {
                    do {
                        const int n = (Stage() >= 3) ? 6 : (Stage() >= 2) ? 4 : 2;
                        const int r = (int)(Random::Get().GetRandomFloat() * (float)n);
                        switch (r) {
                            case 0:  pick = Skill::FeatherStorm; break;
                            case 1:  pick = Skill::MoonGaze;     break;
                            case 2:  pick = Skill::ShadowDash;   break;
                            case 3:  pick = Skill::ScreechWave;  break;
                            case 4:  pick = Skill::StormCall;    break;
                            default: pick = Skill::BloodRain;    break;
                        }
                    } while (pick == lastSkill);
                }
                StartSkill(pick, ctx);
            }
            break;
        }
        case SkillState::Telegraph: {
            skillTimer += dt;
            // 预警期枭王蓄力辉光
            float pulse = 0.5f + 0.5f * std::sin(worldClock * 12.f);
            owlSprite->SetGlow(hitFlashTimer > 0.f ? 2.0f : 1.4f * pulse);
            float teleDur = 0.f;
            switch (curSkill) {
                case Skill::FeatherStorm: teleDur = 0.8f; break;
                case Skill::MoonGaze:     teleDur = 1.2f; break;
                case Skill::ShadowDash:   teleDur = 1.2f; break;
                case Skill::ScreechWave:  teleDur = 1.0f; break;
                case Skill::StormCall:    teleDur = 1.0f; break;
                default:                  teleDur = 1.0f; break; // BloodRain
            }
            if (skillTimer >= teleDur) {
                skillState = SkillState::Active;
                skillTimer = 0.f;
                ctx.fx->RemoveWeatherMsg(SkillMsgSlot()); // 技能预警闪现结束（B1/B4；其余走自绘预警）
                if (curSkill == Skill::ShadowDash)
                    ApplyClip(dashSprite, Anim::Dash); // 掠影动画自此起播（6 帧 ≈ 0.4s 穿屏）
                if (curSkill == Skill::StormCall) {
                    // 三落点按蝙蝠结束时刻的位置展开（§5 B5：蝙蝠当前位置 x、x±140）
                    const float base = std::clamp(ctx.batPos.x, 100.f + StormSpread,
                                                  ctx.screenW - 100.f - StormSpread);
                    stormXs[0] = base - StormSpread;
                    stormXs[1] = base;
                    stormXs[2] = base + StormSpread;
                }
                if (curSkill == Skill::FeatherStorm) {
                    // B1 释放：扇形羽刃（§5）
                    int n = (Stage() >= 3) ? 7 : 5;
                    float speed = 260.f * ((Stage() >= 3) ? 1.15f : 1.0f);
                    float baseAng = std::atan2(ctx.batPos.y - bossY,
                                               ctx.batPos.x - BossPosX);
                    float spread = 40.f * 3.14159f / 180.f; // 总张角 40°
                    int fired = 0;
                    for (auto& f : feathers.items) {
                        if (f.alive) continue;
                        float t = (n > 1) ? (float)fired / (float)(n - 1) : 0.5f;
                        float ang = baseAng + (t - 0.5f) * spread;
                        f.vel = Vec2{std::cos(ang) * speed, std::sin(ang) * speed};
                        f.pos = Vec2{BossPosX - BossHitHalfW, bossY};
                        f.alive = true;
                        if (++fired >= n) break;
                    }
                    // 羽刃即刻倾泻完毕 → 直接进入力竭
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                    ctx.fx->PlaySFX((int)SFX::Thunder, 0.5f);
                }
            }
            break;
        }
        case SkillState::Active: {
            skillTimer += dt;
            owlSprite->SetGlow(hitFlashTimer > 0.f ? 2.0f : 0.f);
            if (curSkill == Skill::MoonGaze) {
                // B2：0.6s 静止激光幕 → 120px/s 扫掠 240px（总约 2.6s）
                const float hold = 0.6f;
                const float sweep = 240.f / 120.f;
                if (skillTimer >= hold) {
                    gazeY += gazeDir * 120.f * dt; // 扫掠推进
                    // 光带钳制在玩法区（地面之上/顶部之下），避免扫出可玩区域
                    gazeY = std::clamp(gazeY, ctx.terrainY + 60.f, ctx.screenH - 60.f);
                }
                if (skillTimer >= hold + sweep) {
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                }
            } else if (curSkill == Skill::ShadowDash) {
                // B3：0.4s 化作横向掠影穿屏（判定=暗影带本体，见 UpdateProjectiles）
                if (skillTimer >= ShadowDashDur) {
                    bossY = std::clamp(shadowBandY, 200.f, 760.f); // 右缘重现：浮游相位对齐掠袭高度
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                }
            } else if (curSkill == Skill::ScreechWave) {
                // B4：3 道音波按 0.6s 间隔依次发出（缺口已起手定好），3.0s 后力竭
                static const float kLaunch[3] = {0.f, 0.6f, 1.2f};
                while (screechSpawned < 3 && skillTimer >= kLaunch[screechSpawned]) {
                    screeches[screechSpawned].alive = true;
                    screeches[screechSpawned].age = 0.f;
                    screechSpawned++;
                }
                if (skillTimer >= ScreechActiveDur) {
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                }
            } else if (curSkill == Skill::StormCall) {
                // B5 雷暴召唤（§5）：0.8s 起三道雷按 0.5s 间隔逐次落下
                // （预警虚线由本侧自绘先行展示；落雷经 IStageFeedback 收口，
                //   场景转发到雨系统管线：危险窗/音效/渲染复用既有路径）
                IStageFeedback& fx = *ctx_->fx;
                while (stormStruck < 3 &&
                       skillTimer >= StormBoltFirst + StormBoltGap * (float)stormStruck) {
                    fx.RequestLightningStrike(stormXs[stormStruck]);
                    stormStruck++;
                }
                if (skillTimer >= StormCallActiveDur) {
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                }
            } else {
                // B6 血月坠羽（§5）：标记列每 0.5s 换一批，列内按 0.3s/根持续生成坠羽，3.0s 后力竭
                bloodReroll -= dt;
                bloodSpawn -= dt;
                if (bloodReroll <= 0.f) {
                    bloodReroll = BloodRerollCd;
                    RollBloodCols(ctx);
                }
                if (bloodSpawn <= 0.f) {
                    bloodSpawn = BloodSpawnCd;
                    for (float colX : bloodCols) {
                        // 列内随机水平偏移（列宽 60，居中 ±18），羽不至于都压在中心线
                        const float x = colX + (Random::Get().GetRandomFloat() - 0.5f) * 36.f;
                        for (auto& d : bloodDrops) {
                            if (!d.alive) {
                                d.alive = true;
                                d.pos = Vec2{x, ctx.screenH + 20.f};
                                break;
                            }
                        }
                    }
                }
                if (skillTimer >= BloodRainActiveDur) {
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                }
            }
            break;
        }
        case SkillState::Fatigue: {
            skillTimer += dt;
            owlSprite->SetGlow(hitFlashTimer > 0.f ? 2.0f : 0.f);
            if (skillTimer >= FatigueDur) {
                skillState = SkillState::Idle;
                intervalTimer = SkillInterval();
                fatigueOutTimer = FatigueOutDur; // 反向展开过渡，软接力到 Idle 动画首帧
            }
            break;
        }
    }
}

// ===== 弹幕与判定 =====

void OwlKing::UpdateProjectiles(float dt, const EnemyContext& ctx) {
    const float batR = ctx.batRadius * 0.6f;

    // 羽刃：推进 + 弹反/命中蝙蝠 + 弹回羽刃反伤本体 + 出界回收
    for (auto& f : feathers.items) {
        if (!f.alive) continue;
        f.pos += Vec2{f.vel.x * dt, f.vel.y * dt};
        float dx = f.pos.x - ctx.batPos.x;
        float dy = f.pos.y - ctx.batPos.y;
        // 弹反（§4.1 三期）：俯冲无敌帧内触羽 → 羽刃原速弹回反伤本体
        if (!f.parried && ctx.batParry &&
            dx * dx + dy * dy < (14.f + batR) * (14.f + batR)) {
            f.parried = true;
            f.vel = Vec2{-f.vel.x, -f.vel.y}; // 原速弹回
            ctx.fx->PlaySFX((int)SFX::Point, 0.7f);
            ctx.fx->FloatTextAt("弹反！", f.pos + Vec2{0.f, 26.f},
                                1.f, 0.95f, 0.55f, 20.f, 0.8f);
            continue;
        }
        if (!f.parried &&
            dx * dx + dy * dy < (14.f + batR) * (14.f + batR)) {
            DamageBat(ctx, 0.f);
            f.alive = false; // 命中即消散
            continue;
        }
        // 弹回羽刃同样需进入本体包围盒（与音爆同规则，含纵向判定）
        if (f.parried && f.pos.x >= BossPosX - BossHitHalfW &&
            std::fabs(f.pos.y - bossY) < BossH * 0.5f) {
            DamageBoss(ParryDamage, f.pos); // 弹回羽刃反伤本体
            f.alive = false;
            continue;
        }
        if (f.pos.x < -30.f || f.pos.x > ctx.screenW + 40.f ||
            f.pos.y < -30.f || f.pos.y > ctx.screenH + 30.f) {
            f.alive = false;
        }
    }

    // B6 坠羽：下落 + 命中蝙蝠 + 落地回收
    for (auto& d : bloodDrops) {
        if (!d.alive) continue;
        d.pos.y -= BloodDropSpeed * dt;
        if (d.pos.y < ctx.terrainY - 10.f) {
            d.alive = false;
            continue;
        }
        float dx = d.pos.x - ctx.batPos.x;
        float dy = d.pos.y - ctx.batPos.y;
        if (dx * dx + dy * dy < (12.f + batR) * (12.f + batR)) {
            DamageBat(ctx, 0.f);
            d.alive = false;
        }
    }

    // 音爆：推进 → 沿途击碎羽刃（至多 2 枚）→ 命中枭王
    for (auto& w : waves.items) {
        if (!w.alive) continue;
        w.pos.x += SonicSpeed * dt;
        for (auto& f : feathers.items) {
            if (!f.alive || w.destroyed >= 2) continue;
            float dx = f.pos.x - w.pos.x;
            float dy = f.pos.y - w.pos.y;
            if (dx * dx + dy * dy < 28.f * 28.f) {
                f.alive = false;
                w.destroyed++;
            }
        }
        // 命中判定：音爆需真实进入本体包围盒——越过受击左缘（x≥360）
        // 且位于本体纵向范围（|dy| < BossH/2）；此前仅判 x，右半屏全高皆可命中
        if (w.pos.x >= BossPosX - BossHitHalfW &&
            std::fabs(w.pos.y - bossY) < BossH * 0.5f) {
            DamageBoss(SonicDamage, w.pos);
            w.alive = false;
            continue;
        }
        if (w.pos.x > ctx.screenW + 40.f) w.alive = false;
    }

    // B2 激光幕判定（仅 Active 态）
    if (phase == Phase::Engagement && curSkill == Skill::MoonGaze &&
        skillState == SkillState::Active) {
        if (std::fabs(ctx.batPos.y - gazeY) < 40.f + batR) {
            DamageBat(ctx, 0.f);
        }
    }

    // 俯冲撞击（§4.4 B6-a）：俯冲中的蝙蝠撞上本体 → 10 伤害 + 向左弹开 160px（防贴脸刷伤害）
    if (ctx.batDiving &&
        std::fabs(ctx.batPos.x - BossPosX) < BossHitHalfW &&
        std::fabs(ctx.batPos.y - bossY) < BossH * 0.5f) {
        DamageBoss(DiveHitDamage, ctx.batPos);
        ctx.fx->Shake(8.0f);
        ctx.fx->BounceBat(-160.f, 0.f); // 弹开方向固定向左
    }

    // 巨化冲撞（§4.5 二期调整）：巨化蝙蝠任意方向撞上本体 → 20 伤害，撞击一次后巨化立即结束
    // （ctx.batRadius 已按巨化体型放大；交战期巨化本就免伤，撞击是巨化期的唯一输出手段）
    if (ctx.batGiant &&
        std::fabs(ctx.batPos.x - BossPosX) < BossHitHalfW + ctx.batRadius &&
        std::fabs(ctx.batPos.y - bossY) < BossH * 0.5f + ctx.batRadius) {
        DamageBoss(GiantHitDamage, ctx.batPos);
        ctx.fx->Shake(8.0f);
        ctx.fx->EndGiant(); // 撞击一次后巨化立刻消失
    }

    // B3 暗影掠袭判定（Active 期：全宽暗影带即本体，高 140）
    if (curSkill == Skill::ShadowDash && skillState == SkillState::Active) {
        if (std::fabs(ctx.batPos.y - shadowBandY) < ShadowBandHalf + batR) {
            DamageBat(ctx, 0.f);
        }
    }

    // B4 音波弧：扩张 + 缺口穿越判定（波体独立于技能状态存续至超时）
    for (auto& w : screeches) {
        if (!w.alive) continue;
        w.age += dt;
        if (w.age > 5.f) { w.alive = false; continue; }
        if (ctx.batPos.x >= BossPosX - BossHitHalfW) continue; // 弧仅朝左半场扩散
        const float dx = ctx.batPos.x - (BossPosX - BossHitHalfW);
        const float dy = ctx.batPos.y - w.centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (std::fabs(dist - ScreechSpeed * w.age) < 14.f + batR &&
            std::fabs(ctx.batPos.y - w.gapY) > ScreechGapHalf + batR * 0.5f) {
            DamageBat(ctx, 0.f); // 缺口（130px + 半幅宽容）内穿越则安全
        }
    }
}

void OwlKing::UpdateCrystals(float dt, const EnemyContext& ctx) {
    // 生成：Warning + Engagement 期，每 4s 一颗、场上至多 CrystalMax（§4.2）
    crystalTimer += dt;
    if (crystalTimer >= CrystalInterval) {
        crystalTimer = 0.f;
        int aliveCnt = 0;
        for (auto& c : crystals.items) if (c.alive) aliveCnt++;
        for (auto& c : crystals.items) {
            if (c.alive) continue;
            if (aliveCnt >= CrystalMax) break;
            c.pos.x = 80.f + 280.f * Random::Get().GetRandomFloat(); // 蝙蝠活动区 x∈[80,360]
            float lo = ctx.terrainY + 110.f;
            float hi = std::max(lo + 30.f, ctx.screenH - 120.f);
            c.pos.y = lo + (hi - lo) * Random::Get().GetRandomFloat();
            c.bob = Random::Get().GetRandomFloat() * 6.28f;
            c.alive = true;
            aliveCnt++;
        }
    }

    // 浮动 + 拾取（不随世界滚动）
    const float batR = ctx.batRadius * 0.5f;
    for (auto& c : crystals.items) {
        if (!c.alive) continue;
        c.bob += dt;
        float dx = c.pos.x - ctx.batPos.x;
        float dy = c.pos.y - ctx.batPos.y;
        if (dx * dx + dy * dy < (34.f + batR) * (34.f + batR)) {
            c.alive = false;
            sonicCharge = std::min(SonicChargeMax, sonicCharge + 1);
            IStageFeedback& fx = *ctx_->fx;
            fx.AddCoins(CrystalCoin); // 顺带金币（§4.2；对齐巨化星惯例，不另发飘字）
            fx.PlaySFX((int)SFX::Collect, 0.8f);
            fx.FloatTextAt("声波 +1", c.pos + Vec2{0.f, 34.f},
                         0.57f, 0.85f, 0.95f, 20.f, 1.0f);
            fx.OnEnemyItemCollected(c.pos); // 预留：连击/成就通道（一期空操作）
        }
    }
}

void OwlKing::DamageBat(const EnemyContext& ctx, float edgeY) {
    if (ctx.batInvincible || hitInvTimer > 0.f) return;
    if (batHearts <= 0) return;
    batHearts--;
    hitInvTimer = HitInvulnDur; // 本侧帧内精确免伤门（蝙蝠侧闪烁无敌经 GrantBatInvuln 另行授予）
    // 受击反馈（§6/§8）：音效/抖屏 10/飘字/无敌闪烁/击退/心形碎裂
    IStageFeedback& fx = *ctx_->fx;
    fx.PlaySFX((int)SFX::Death, 0.7f);
    fx.Shake(10.0f);                                   // §8 抖动表：受击 10
    fx.FloatTextAt("受击 -1", ctx.batPos + Vec2{0.f, 60.f},
                 0.87f, 0.35f, 0.35f, 24.f, 1.0f);
    fx.GrantBatInvuln(HitInvulnDur);                   // §6：1.2s 受击无敌（蝙蝠辉光闪烁反馈）
    if (edgeY != 0.f) {
        fx.BounceBat(0.f, (edgeY < 0.f) ? 90.f : -90.f); // 撞地弹起 / 撞顶下压
    } else {
        fx.BounceBat(-KnockBackX, 0.f);                  // §6：技能命中向左击退 100px
    }
    heartBreakIdx = batHearts;                           // §8：刚被击碎的心形槽位（碎裂残影）
    heartBreakTimer = HeartBreakDur;
    if (batHearts <= 0) pendingKill_ = true; // 心数耗尽 -> Update 收尾统一致死
}

void OwlKing::DamageBoss(int amount, const Vec2& at) {
    if (phase != Phase::Engagement) return;
    int dmg = (int)((float)amount * DamageMult());
    hp -= dmg;
    combatHits++;
    hitFlashTimer = 0.1f;
    // 阶段跃迁（>60% → ≤60% → ≤25%）：血条短暂抖动（§8；hp>0 守卫排除击杀一击）
    const int st = Stage();
    if (hp > 0 && st > lastStage) barShakeTimer = BarShakeDur;
    lastStage = st;
    if (hp > 0) flinchTimer = FlinchDur; // 受击 flinch（击杀一击直接进死亡演出，不再后仰）
    // 命中反馈（原场景 BossHit 事件分支迁入）
    IStageFeedback& fx = *ctx_->fx;
    fx.PlaySFX((int)SFX::Point, 0.8f);
    fx.Shake(6.0f);                                    // §8 抖动表：命中 6
    fx.FloatTextAt("-" + std::to_string(dmg), at + Vec2{0.f, 40.f},
                 0.95f, 0.82f, 0.35f, 22.f, 1.0f);
    if (hp <= 0) {
        hp = 0;
        EnterDefeat();
    }
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

bool OwlKing::TryFireSonic(const Vec2& from) {
    if (phase != Phase::Engagement) return false;
    if (sonicCharge <= 0 || sonicCdTimer > 0.f) return false;
    for (size_t i = 0; i < waves.items.size(); ++i) {
        if (waves.items[i].alive) continue;
        waves.items[i].alive = true;
        waves.items[i].pos = Vec2{from.x + 40.f, from.y};
        waves.items[i].destroyed = 0;
        waves.sprites[i]->SetPosition(waves.items[i].pos.x, waves.items[i].pos.y);
        waves.sprites[i]->GetAnimation()->Reset(); // 重播脉冲
        sonicCharge--;
        sonicCdTimer = SonicCooldown;
        return true;
    }
    return false; // 池满（理论不可能：波飞行短于冷却）
}

// ===== 世界层渲染（渲染期为表现路径，窗口尺寸直读 Config） =====

void OwlKing::RenderWorld(SpriteRenderer& renderer) {
    const float W = (float)Config::GetInstance().GetScreenWidth();
    const float H = (float)Config::GetInstance().GetScreenHeight();

    // 血月：Warning 淡入 → Engagement 常驻 → Defeat/Leave 淡出
    // B6 血月坠羽：血月增亮泛红（§5 B6 预警意象）
    const bool bloodMoonBoost = (curSkill == Skill::BloodRain &&
                                 (skillState == SkillState::Telegraph ||
                                  skillState == SkillState::Active));
    float moonA = 0.f;
    if (phase == Phase::Warning)       moonA = 0.9f * std::min(1.f, phaseTimer / WarningDur);
    else if (phase == Phase::Engagement) moonA = bloodMoonBoost ? 1.0f : 0.9f;
    else if (phase == Phase::Defeat || phase == Phase::Leave)
        moonA = 0.9f * std::max(0.f, 1.f - phaseTimer);
    if (moonSprite && moonA > 0.01f) {
        moonSprite->SetPosition(W - 105.f, H - 150.f); // GL y 向上：屏幕右上角
        if (bloodMoonBoost)
            moonSprite->SetColor(1.f, 0.55f, 0.5f, moonA);
        else
            moonSprite->SetColor(1.f, 1.f, 1.f, moonA);
        renderer.Draw_UV(moonSprite);
    }

    // 回声水晶（浮动）
    for (size_t i = 0; i < crystals.items.size(); ++i) {
        if (!crystals.items[i].alive) continue;
        float bobY = crystals.items[i].pos.y + 8.f * std::sin(crystals.items[i].bob * 1.6f);
        crystals.sprites[i]->SetPosition(crystals.items[i].pos.x, bobY);
        renderer.Draw_UV(crystals.sprites[i]);
    }

    // 枭王本体：交战淡入（力竭半透明）/ 死亡消散 / 离场淡出
    float alpha = 0.f;
    if (phase == Phase::Engagement) {
        alpha = std::min(1.f, phaseTimer / EngageFadeIn)
              * (skillState == SkillState::Fatigue ? FatigueAlpha : 1.f);
        if (curSkill == Skill::ShadowDash) {
            if (skillState == SkillState::Telegraph)
                alpha *= std::max(0.f, 1.f - skillTimer / 0.3f); // 雾化消失（§5 B3 时隐时现演出）
            else if (skillState == SkillState::Active)
                alpha = 0.f; // 化作掠影（下方单独绘制）
        }
    } else if (phase == Phase::Defeat) {
        alpha = 1.f;
    } else if (phase == Phase::Leave) {
        alpha = std::max(0.f, 1.f - phaseTimer / LeaveDur);
    }
    // 入离场渲染位移（§2 降临/离场演出）：仅视觉偏移，受击判定仍以 BossPosX 为准。
    // 入场随雾化淡入自右屏外滑入、离场随淡出掠出——配合 Swoop 振翅循环才读作「俯冲降临/饱食离去」，
    // 否则本体原地半透明拍翼，Swoop 素材形同未用。
    float renderX = BossPosX;
    if (phase == Phase::Engagement && phaseTimer < EngageFadeIn)
        renderX += SwoopSlide * (1.f - phaseTimer / EngageFadeIn);
    else if (phase == Phase::Leave)
        renderX += SwoopSlide * (phaseTimer / LeaveDur);

    if (owlSprite && alpha > 0.01f) {
        owlSprite->SetPosition(renderX, bossY);
        // 三阶段狂暴（§3）：体表泛红
        if (phase == Phase::Engagement && Stage() >= 3)
            owlSprite->SetColor(1.f, 0.68f, 0.68f, alpha);
        else
            owlSprite->SetColor(1.f, 1.f, 1.f, alpha);
        renderer.Draw_UV(owlSprite);
    }


    // B3 暗影掠袭：巨大枭影横向掠过（Active 期，暗紫残影；独立掠影精灵，帖图朝左与弹道一致）
    if (phase == Phase::Engagement && curSkill == Skill::ShadowDash &&
        skillState == SkillState::Active && dashSprite) {
        const float t = std::clamp(skillTimer / ShadowDashDur, 0.f, 1.f);
        const float dashX = (BossPosX + 120.f) - t * (BossPosX + 120.f + 260.f);
        dashSprite->SetPosition(dashX, shadowBandY);
        dashSprite->SetColor(0.85f, 0.55f, 1.0f, 0.75f);
        renderer.Draw_UV(dashSprite);
    }

    // 羽刃（贴图尖端朝上，旋转对准飞行方向）
    for (size_t i = 0; i < feathers.items.size(); ++i) {
        if (!feathers.items[i].alive) continue;
        feathers.sprites[i]->SetPosition(feathers.items[i].pos.x, feathers.items[i].pos.y);
        float ang = std::atan2(feathers.items[i].vel.y, feathers.items[i].vel.x) - 3.14159f * 0.5f;
        feathers.sprites[i]->SetRotation(Vec2{std::cos(ang), std::sin(ang)});
        renderer.Draw_UV(feathers.sprites[i]);
    }

    // B6 血月坠羽（红色羽刃朝下坠落；旋转惯例同 B1 羽刃，速度 (0,-1) 对应 (-1,0)）
    for (size_t i = 0; i < bloodDrops.size(); ++i) {
        if (!bloodDrops[i].alive) continue;
        bloodSprites[i]->SetPosition(bloodDrops[i].pos.x, bloodDrops[i].pos.y);
        bloodSprites[i]->SetRotation(Vec2{-1.f, 0.f});
        bloodSprites[i]->SetColor(1.f, 0.45f, 0.45f, 0.95f);
        renderer.Draw_UV(bloodSprites[i]);
    }

    // 音爆冲击波
    for (size_t i = 0; i < waves.items.size(); ++i) {
        if (!waves.items[i].alive) continue;
        waves.sprites[i]->SetPosition(waves.items[i].pos.x, waves.items[i].pos.y);
        renderer.Draw_UV(waves.sprites[i]);
    }
}

// ===== 前景层 UI：血条/心数/声波槽/预警带/渐晕（§8） =====

void OwlKing::RenderUI() {
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W = (float)Config::GetInstance().GetScreenWidth();
    const float H = (float)Config::GetInstance().GetScreenHeight();

    // ---- Warning 期：右缘暗红渐晕（血月意象） ----
    if (phase == Phase::Warning) {
        float t = std::min(1.f, phaseTimer / WarningDur);
        ImU32 colR = ImGui::GetColorU32(ImVec4(0.55f, 0.05f, 0.08f, 0.42f * t));
        ImU32 colT = ImGui::GetColorU32(ImVec4(0.55f, 0.05f, 0.08f, 0.f));
        dl->AddRectFilledMultiColor(ImVec2(W * 0.55f, 0.f), ImVec2(W, H),
                                    colT, colR, colR, colT);
    }

    // ---- 声波槽：顶部中央心数（y142）下方一行（充能 >0 或战斗相关相位时显示） ----
    // 置于顶部中央而非左上角，避免与天气事件消息盒（y190 起）纵向冲突
    if (sonicCharge > 0 || phase == Phase::Warning || phase == Phase::Engagement) {
        const float rowY = 190.f;
        const float cx0 = W * 0.5f - 34.f; // 3 菱形整体居中
        for (int i = 0; i < SonicChargeMax; ++i) {
            const float cx = cx0 + i * 34.f;
            const bool filled = i < sonicCharge;
            // 菱形槽（旋转 45° 方块）
            const float r = 10.f;
            ImU32 fill = filled ? UITheme::ToImU32({0.57f, 0.85f, 0.95f})
                                : UITheme::ToImU32({0.12f, 0.14f, 0.20f}, 0.9f);
            dl->AddQuadFilled(ImVec2(cx, rowY - r), ImVec2(cx + r, rowY),
                              ImVec2(cx, rowY + r), ImVec2(cx - r, rowY), fill);
            dl->AddQuad(ImVec2(cx, rowY - r), ImVec2(cx + r, rowY),
                        ImVec2(cx, rowY + r), ImVec2(cx - r, rowY),
                        filled ? UITheme::ToImU32({0.75f, 0.95f, 1.0f})
                               : UITheme::ToImU32(UITheme::AmberDim, 0.6f), 1.5f);
        }
        // 小标签（金色，带阴影）——置于菱形右侧
        const char* label = "声波";
        const float lx = cx0 + 3.f * 34.f + 12.f;
        dl->AddText(font, 15.f, ImVec2(lx + 1, rowY - 8.f + 1),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), label);
        dl->AddText(font, 15.f, ImVec2(lx, rowY - 8.f),
                    UITheme::ToImU32(UITheme::AmberDim), label);
    }

    // ---- 枭王血条 + 心数：仅交战/击杀演出期 ----
    if (phase == Phase::Engagement || phase == Phase::Defeat) {
        const float barW = 380.f, barH = 20.f;
        // 阶段跃迁血条抖动（§8）：确定性高频正弦 + 线性衰减（渲染路径不取随机数）
        float shakeX = 0.f, shakeY = 0.f;
        if (barShakeTimer > 0.f) {
            const float amp = BarShakeAmp * (barShakeTimer / BarShakeDur);
            shakeX = amp * std::sin(worldClock * 80.f);
            shakeY = amp * 0.6f * std::sin(worldClock * 113.f + 1.7f);
        }
        const float barX = W * 0.5f + shakeX, barY = 116.f + shakeY; // 倒计时/连击 HUD 下方
        UIBuilder::DrawRoundedRect(barX, barY, barW, barH, 10.f,
            UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
            (skillState == SkillState::Fatigue)
                ? UITheme::ToImU32(UITheme::Amber, 0.9f)  // 力竭：金色描边提示伤害加成
                : UITheme::ToImU32(UITheme::Blood, 0.9f),
            2.0f);

        // 血量填充（血红；力竭期微微脉动）
        float frac = hpMax > 0 ? std::clamp((float)hp / (float)hpMax, 0.f, 1.f) : 0.f;
        float pulse = (skillState == SkillState::Fatigue)
            ? 0.85f + 0.15f * (0.5f + 0.5f * std::sin(worldClock * 8.f)) : 1.f;
        if (frac > 0.005f) {
            float fw = (barW - 8.f) * frac;
            dl->AddRectFilled(ImVec2(barX - barW * 0.5f + 4.f, barY - barH * 0.5f + 4.f),
                              ImVec2(barX - barW * 0.5f + 4.f + fw, barY + barH * 0.5f - 4.f),
                              UITheme::ToImU32(UITheme::Blood, 0.95f * pulse), 6.f);
        }

        // 枭王名号（血条上方，居中小字）
        UIBuilder::DrawCenteredText("血月枭王", barX, barY - 20.f, 16.f,
                                    UITheme::ToImU32(UITheme::Moonlight, 0.85f));

        // 心数（血条正下方 3 颗；遭遇期专属血量）
        if (phase == Phase::Engagement) {
            const float hy = 142.f;
            // 心形 = 双圆 + 下三角（s 缩放；shadow=右下暗影层）
            auto heartShape = [&](float cx, float cy, float s, ImU32 col, bool shadow) {
                const float o = shadow ? 1.f : 0.f;
                dl->AddCircleFilled(ImVec2(cx - 6.f * s + o, cy - 4.f * s + o), 7.f * s, col, 12);
                dl->AddCircleFilled(ImVec2(cx + 6.f * s + o, cy - 4.f * s + o), 7.f * s, col, 12);
                dl->AddTriangleFilled(ImVec2(cx - 12.f * s + o, cy - 2.f * s + o),
                                      ImVec2(cx + 12.f * s + o, cy - 2.f * s + o),
                                      ImVec2(cx + o, cy + 12.f * s + o), col);
            };
            for (int i = 0; i < BatHeartsMax; ++i) {
                const float cx = W * 0.5f + (float)(i - 1) * 34.f;
                if (i < batHearts) {
                    heartShape(cx, hy, 1.f, UITheme::ToImU32(UITheme::Blood, 0.55f), true);  // 暗影层
                    heartShape(cx, hy, 1.f, UITheme::ToImU32(UITheme::Blood), false);
                } else {
                    if (i == heartBreakIdx && heartBreakTimer > 0.f) {
                        // §8 心形碎裂：残影放大淡出（缩放 1→1.6，alpha 0.9→0）
                        const float k = heartBreakTimer / HeartBreakDur;
                        heartShape(cx, hy, 1.f + 0.6f * (1.f - k),
                                   UITheme::ToImU32(UITheme::Blood, 0.9f * k), false);
                    }
                    ImU32 empty = UITheme::ToImU32(UITheme::AmberDim, 0.55f);
                    dl->AddCircle(ImVec2(cx - 6.f, hy - 4.f), 7.f, empty, 12, 1.5f);
                    dl->AddCircle(ImVec2(cx + 6.f, hy - 4.f), 7.f, empty, 12, 1.5f);
                    dl->AddTriangle(ImVec2(cx - 12.f, hy - 2.f),
                                    ImVec2(cx + 12.f, hy - 2.f),
                                    ImVec2(cx, hy + 12.f), empty, 1.5f);
                }
            }
        }
    }

    // ---- B2 血月凝视：预警虚线光带 / 激活激光幕（世界 y 转 ImGui 屏幕 y） ----
    if (phase == Phase::Engagement && curSkill == Skill::MoonGaze) {
        const float sy = H - gazeY;      // GL y 向上 -> 屏幕 y 向下
        const float half = 40.f;
        if (skillState == SkillState::Telegraph) {
            // 预警：半透明红带 + 呼吸脉冲（§5 B2）
            float t = std::min(1.f, skillTimer / 1.2f);
            float pulse = 0.5f + 0.5f * std::sin(worldClock * 10.f);
            ImU32 fill = ImGui::GetColorU32(ImVec4(0.76f, 0.18f, 0.18f,
                                                   (0.06f + 0.08f * pulse) * (0.5f + 0.5f * t)));
            dl->AddRectFilled(ImVec2(0.f, sy - half), ImVec2(W, sy + half), fill);
            dl->AddRect(ImVec2(0.f, sy - half), ImVec2(W, sy + half),
                        UITheme::ToImU32(UITheme::Blood, 0.5f + 0.4f * pulse), 0.f, 2.f);
        } else if (skillState == SkillState::Active) {
            // 激活：血红激光幕 + 亮边 + 白热核心线
            dl->AddRectFilled(ImVec2(0.f, sy - half), ImVec2(W, sy + half),
                              ImGui::GetColorU32(ImVec4(0.76f, 0.15f, 0.15f, 0.42f)));
            dl->AddLine(ImVec2(0.f, sy - half), ImVec2(W, sy - half),
                        UITheme::ToImU32(UITheme::Blood), 3.f);
            dl->AddLine(ImVec2(0.f, sy + half), ImVec2(W, sy + half),
                        UITheme::ToImU32(UITheme::Blood), 3.f);
            dl->AddLine(ImVec2(0.f, sy), ImVec2(W, sy),
                        ImGui::GetColorU32(ImVec4(1.f, 0.85f, 0.8f, 0.55f)), 2.f);
        }
    }

    // ---- B3 暗影掠袭：全宽暗影带（预警紫色闪烁 / 掠袭增亮） ----
    if (phase == Phase::Engagement && curSkill == Skill::ShadowDash) {
        const float syB3 = H - shadowBandY;
        const float halfB3 = ShadowBandHalf;
        if (skillState == SkillState::Telegraph) {
            float pulse = 0.5f + 0.5f * std::sin(worldClock * 10.f);
            float fade = std::min(1.f, skillTimer / 1.2f);
            dl->AddRectFilled(ImVec2(0.f, syB3 - halfB3), ImVec2(W, syB3 + halfB3),
                              ImGui::GetColorU32(ImVec4(0.45f, 0.20f, 0.65f,
                                                        (0.06f + 0.10f * pulse) * (0.5f + 0.5f * fade))));
            dl->AddRect(ImVec2(0.f, syB3 - halfB3), ImVec2(W, syB3 + halfB3),
                        ImGui::GetColorU32(ImVec4(0.62f, 0.35f, 0.85f, 0.5f + 0.4f * pulse)), 0.f, 2.f);
        } else if (skillState == SkillState::Active) {
            dl->AddRectFilled(ImVec2(0.f, syB3 - halfB3), ImVec2(W, syB3 + halfB3),
                              ImGui::GetColorU32(ImVec4(0.35f, 0.15f, 0.55f, 0.42f)));
            dl->AddLine(ImVec2(0.f, syB3 - halfB3), ImVec2(W, syB3 - halfB3),
                        ImGui::GetColorU32(ImVec4(0.62f, 0.35f, 0.85f, 0.9f)), 2.f);
            dl->AddLine(ImVec2(0.f, syB3 + halfB3), ImVec2(W, syB3 + halfB3),
                        ImGui::GetColorU32(ImVec4(0.62f, 0.35f, 0.85f, 0.9f)), 2.f);
        }
    }

    // ---- B4 枭啸音波：喙前聚气（预警）+ 缺口预标记 / 缺口弧（发出后） ----
    if (phase == Phase::Engagement && curSkill == Skill::ScreechWave) {
        const float cxS = BossPosX - BossHitHalfW;
        if (skillState == SkillState::Telegraph) {
            for (int i = 0; i < 8; ++i) { // 喙前白色音波粒子聚拢（§5 B4 预警）
                float ph = worldClock * 6.f + (float)i * 0.9f;
                float px = cxS - 14.f - 10.f * (0.5f + 0.5f * std::sin(ph));
                float py = H - (bossY + 16.f * std::sin(ph * 1.7f));
                dl->AddCircleFilled(ImVec2(px, py), 2.5f + 1.5f * (0.5f + 0.5f * std::sin(ph * 2.3f)),
                                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.55f)), 10);
            }
        }
        if (skillState == SkillState::Active) {
            // 待发射波的缺口预标记（发射间隔 0.6s，标记全程可见，满足 ≥0.3s 提前量）
            for (int i = screechSpawned; i < 3; ++i) {
                float gy = H - screeches[i].gapY;
                float a = 0.35f + 0.3f * (0.5f + 0.5f * std::sin(worldClock * 8.f + (float)i));
                dl->AddLine(ImVec2(64.f, gy - 18.f), ImVec2(64.f, gy + 18.f),
                            ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, a)), 2.f);
            }
        }
        // 已发射波：白色音波弧（缺口处断开）+ 缺口白色标记
        for (const auto& w : screeches) {
            if (!w.alive) continue;
            const float r = ScreechSpeed * w.age;
            if (r < 8.f) continue;
            const float cyS = H - w.centerY;
            const float dyG = w.gapY - w.centerY;
            const bool gapOnArc = std::fabs(dyG) < r;
            const float a0 = 3.14159265f * 0.5f, a1 = 3.14159265f * 1.5f; // 左半圆（世界角）
            float thetaG = 0.f, delta = 0.f;
            if (gapOnArc) {
                thetaG = std::atan2(dyG, -std::sqrt(r * r - dyG * dyG));
                delta = std::asin(std::clamp(ScreechGapHalf / r, 0.f, 1.f));
            }
            auto stroke = [&](float sa, float ea) {
                if (ea <= sa) return;
                const int steps = std::max(4, (int)((ea - sa) * 30.f));
                dl->PathClear();
                for (int k = 0; k <= steps; ++k) {
                    float a = sa + (ea - sa) * (float)k / (float)steps;
                    dl->PathLineTo(ImVec2(cxS + r * std::cos(a), cyS - r * std::sin(a)));
                }
                dl->PathStroke(ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.55f)), 0, 2.5f);
            };
            if (!gapOnArc) {
                stroke(a0, a1);
            } else {
                stroke(a0, thetaG - delta);
                stroke(thetaG + delta, a1);
                const float mx = cxS + r * std::cos(thetaG), my = cyS - r * std::sin(thetaG);
                dl->AddCircleFilled(ImVec2(mx, my), 4.f,
                                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.85f)), 12);
            }
        }
    }

    // ---- B5 雷暴召唤：三落点预警虚线 +「雷」字（落雷前可见；落雷本体走 RainSystem 折线管线） ----
    if (phase == Phase::Engagement && curSkill == Skill::StormCall &&
        skillState == SkillState::Active) {
        const float groundY = H - Config::GetInstance().GetTerrainHeight();
        for (int i = stormStruck; i < 3; ++i) {
            const float sx = stormXs[i];
            float pulse = 0.5f + 0.5f * std::sin(worldClock * 10.f + (float)i);
            const float seg = 26.f, gap = 14.f;
            for (float y = 10.f; y < groundY - 6.f; y += seg + gap) {
                float y2 = std::min(y + seg, groundY - 6.f);
                dl->AddLine(ImVec2(sx, y), ImVec2(sx, y2),
                            ImGui::GetColorU32(ImVec4(0.94f, 0.72f, 0.31f, 0.5f + 0.4f * pulse)), 2.f);
            }
            UIBuilder::DrawCenteredText("雷", std::clamp(sx, 16.f, W - 16.f),
                                        groundY - 22.0f, 24.0f,
                                        UITheme::ToImU32(UITheme::Amber, 0.5f + 0.5f * pulse));
        }
    }

    // ---- B6 血月坠羽：顶部 4 列红色下落标记（列宽 60，换批即移动） ----
    if (phase == Phase::Engagement && curSkill == Skill::BloodRain &&
        (skillState == SkillState::Telegraph || skillState == SkillState::Active)) {
        for (float colX : bloodCols) {
            float pulse = 0.5f + 0.5f * std::sin(worldClock * 9.f + colX * 0.05f);
            ImU32 col = ImGui::GetColorU32(ImVec4(0.95f, 0.35f, 0.35f, 0.45f + 0.4f * pulse));
            dl->AddRectFilled(ImVec2(colX - BloodColW * 0.5f, 0.f),
                              ImVec2(colX + BloodColW * 0.5f, 4.f), col);
            dl->AddTriangleFilled(ImVec2(colX - 10.f, 8.f), ImVec2(colX + 10.f, 8.f),
                                  ImVec2(colX, 26.f), col);
        }
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
