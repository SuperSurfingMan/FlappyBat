#include "scene/stage/PlayerAbilities.h"
#include "core/Config.h"
#include "sprite/BatSprite.h"
#include "audio/AudioSystem.h"
#include "app/PlayerProfile.h"
#include "app/TuningParams.h"
#include "enemy/IStageServices.h"
#include "gameplay/GustPhysics.h"
#include "gameplay/ShieldRules.h"
#include "ui/UITheme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>

void PlayerAbilities::Init(std::shared_ptr<BatSprite> b, IFeedbackFx* feedback,
                          const Config& cfg, PlayerProfile& profile, const TuningParams& tuning) {
    bat = std::move(b);
    fx = feedback;
    cfg_ = &cfg;
    profile_ = &profile;
    tuning_ = &tuning;
}

void PlayerAbilities::Reset() {
    invincibleEnergy = 0.0f;
    invincibleTimer = 0.0f;
    shortShieldTimer = 0.0f;
    shortShieldCooldown = 0.0f;
    parryReleaseAge = -1.0f;
    pulseClock = 0.0f;
    giantTimer = 0.0f;
    slowMoTimer = 0.0f;
    locked_ = false;
}

void PlayerAbilities::Update(float dt, const StageFrame& f, Vec2 gustForce) {
    pulseClock += dt;

    // ===== 无敌护盾（原能量护盾）：只推进自身计时，与短护盾互不影响 =====
    if (invincibleTimer > 0.0f) invincibleTimer = std::max(0.0f, invincibleTimer - dt);

    // ===== 一次性短护盾：仅在玩法帧推进（暂停/入场/结算由场景门控冻结）=====
    if (shortShieldTimer > 0.0f) shortShieldTimer = std::max(0.0f, shortShieldTimer - dt);
    if (shortShieldCooldown > 0.0f) shortShieldCooldown = std::max(0.0f, shortShieldCooldown - dt);
    if (parryReleaseAge >= 0.0f) {
        parryReleaseAge += dt;
        if (parryReleaseAge > shield::kParryGrace) parryReleaseAge = -1.0f;
    }

    UpdateMotion(dt, f, gustForce);

    if (f.input) {
        // 空格：仅用于释放无敌护盾（不再参与振翅）；能量未满时按下无任何效果
        if (f.input->Pressed(GLFW_KEY_SPACE)) {
            TryReleaseInvincible();
        }
        // 短护盾使用独立键位（原俯冲键 S/↓），与空格互不干扰
        if (f.input->Pressed(GLFW_KEY_S) || f.input->Pressed(GLFW_KEY_DOWN)) {
            TryReleaseShortShield();
        }
    }
}

void PlayerAbilities::UpdateMotion(float dt, const StageFrame& f, Vec2 gustForce) {
    if (!bat) return;

    // 阵风四向：推力折算成恒定漂移速度（换算见 gameplay/GustPhysics.h）。
    // 纵向叠加到位置积分、横向叠加到横移速度——两者都不写入会被
    // JumpNow()/SetHorizontalSpeed() 每帧覆写的状态，因此整段阵风持续生效。
    const float driftY = gust::DriftFromForce(gustForce.y);
    const float driftX = gust::DriftFromForce(gustForce.x);

    if (!bat->GetHitState()) {
        bat->Falling(dt, driftY); // 重力 + 阵风漂移 + 窗口边界钳制
        // 振翅：Auto Play 武装时取策略意图；否则鼠标左键 / W・↑（空格专用于无敌护盾，不参与振翅）
        bool flap = false;
        if (f.autoPilot) {
            flap = f.autoPilot->jump;
        } else {
            flap = (f.input && f.input->PointerPressed(GLFW_MOUSE_BUTTON_LEFT)) ||
                   (f.input &&
                    (f.input->Pressed(GLFW_KEY_W) ||
                     f.input->Pressed(GLFW_KEY_UP)));
        }
        if (flap) {
            bat->JumpNow();
            if (fx) fx->PlaySFX((int)SFX::Fly, 1.0f);
        }
        // 横移：Auto Play 武装时取策略意图；否则 A/D 或 ←/→ 持续按住
        // （躲避全高雷柱；注意用 Held 持续语义）
        float hDir = 0.0f;
        if (f.autoPilot) {
            hDir = (float)f.autoPilot->horizontal;
        } else if (f.input) {
            if (f.input->Held(GLFW_KEY_A) || f.input->Held(GLFW_KEY_LEFT)) hDir -= 1.0f;
            if (f.input->Held(GLFW_KEY_D) || f.input->Held(GLFW_KEY_RIGHT)) hDir += 1.0f;
        }
        // 横移 = 玩家输入 + 横向阵风漂移。风速恒低于按键满速，按住反方向可对抗；
        // 叠加后横速也驱动渲染倾角，被吹时蝙蝠会自然侧倾。
        bat->SetHorizontalSpeed(hDir * BatSprite::HorizontalMoveSpeed + driftX);
    }
    bat->Update(dt);
}

// =============================================================================
// 无敌护盾（invincible，原能量护盾）——行为与旧版逐条对齐
// =============================================================================

void PlayerAbilities::AddInvincibleEnergy(float amount) {
    if (invincibleTimer > 0.0f) return; // 护盾生效期间不积攒（原行为）
    // B8：充能速度升级提升所有能量获取
    const float mult = 1.0f + InvincibleChargePerLevel *
        (float)profile_->GetLevel(PlayerProfile::ShieldTrack::Charge);
    invincibleEnergy = std::min(1.0f, invincibleEnergy + amount * mult);
}

void PlayerAbilities::TryReleaseInvincible() {
    if (!bat) return;
    // 能量闸门与生效互斥均为原逻辑；巨化期间本身无敌，避免无敌计时被覆盖
    if (locked_ || invincibleEnergy < 1.0f || invincibleTimer > 0.0f) return;
    if (IsGiant()) return;

    const float dur = InvincibleDuration(); // B8：持续时间随升级等级提升
    bat->SetInvincible(dur);
    invincibleTimer = dur;
    invincibleEnergy = 0.0f;
    if (fx) fx->PlaySFX((int)SFX::Collect, 1.0f);
    std::cout << "Invincible shield released! Invincible for " << dur << "s." << std::endl;
}

void PlayerAbilities::GrantInvincible(float dur) {
    if (!bat || dur <= 0.0f) return;
    bat->SetInvincible(dur); // 场景授予的免费无敌：不占能量、不占 HUD 护盾计时
}

float PlayerAbilities::InvincibleDuration() const {
    const int lv = profile_->GetLevel(PlayerProfile::ShieldTrack::Duration);
    return InvincibleBaseDuration + InvincibleDurationPerLevel * (float)lv;
}

bool PlayerAbilities::InvincibleReady() const {
    return !locked_ && !IsGiant() && invincibleEnergy >= 1.0f && invincibleTimer <= 0.0f;
}

// =============================================================================
// 一次性短护盾（shield，CD 制；弹反）——与无敌护盾完全独立
// =============================================================================

void PlayerAbilities::TryReleaseShortShield() {
    if (!bat) return;
    if (locked_ || IsGiant() || shortShieldCooldown > 0.0f || shortShieldTimer > 0.0f) return;

    shortShieldTimer = shield::kDuration;
    shortShieldCooldown = shield::kCooldown; // 每次释放后重置 CD 为 5s
    parryReleaseAge = 0.0f;                  // 弹反窗口起点（怪物侧挂起的子弹据此判定）
    if (fx) {
        fx->PlaySFX((int)SFX::Collect, 1.0f);
        fx->FloatTextAt("短护盾", bat->GetPosition() + Vec2(0.0f, 64.0f),
                        0.62f, 0.85f, 0.98f, 20.0f, 0.6f);
    }
    std::cout << "Short shield up (" << shield::kDuration
              << "s), CD reset to " << shield::kCooldown << "s." << std::endl;
}

bool PlayerAbilities::ConsumeShortShield() {
    if (shortShieldTimer <= 0.0f) return false;
    shortShieldTimer = 0.0f; // 一次受击后立刻消失（CD 不重置）
    // 破盾后的极短免伤：防同一帧内多个伤害源连续命中，且不缩短更长的既有无敌
    if (bat && bat->GetInvincibleRemaining() < shield::kBreakInvuln)
        bat->SetInvincible(shield::kBreakInvuln);
    if (fx && bat) {
        fx->PlaySFX((int)SFX::Collect, 1.0f);
        fx->FloatTextAt("格挡！", bat->GetPosition() + Vec2(0.0f, 62.0f),
                        0.62f, 0.85f, 0.98f, 20.0f, 0.7f);
    }
    return true;
}

bool PlayerAbilities::ParryReleaseWindow() const {
    return parryReleaseAge >= 0.0f && parryReleaseAge <= shield::kParryGrace;
}

float PlayerAbilities::ShortShieldDuration() const { return shield::kDuration; }
float PlayerAbilities::ShortShieldCooldownTotal() const { return shield::kCooldown; }

bool PlayerAbilities::ShortShieldReady() const {
    return !locked_ && !IsGiant() && shortShieldCooldown <= 0.0f && shortShieldTimer <= 0.0f;
}

// =============================================================================
// 通用能力
// =============================================================================

void PlayerAbilities::GrantInvuln(float dur) {
    if (!bat || dur <= 0.0f) return;
    if (bat->GetInvincibleRemaining() >= dur) return;
    bat->SetInvincible(dur);
}

void PlayerAbilities::TriggerSlowMo(float dur) {
    slowMoTimer = std::max(slowMoTimer, dur);
}

void PlayerAbilities::TickSlowMo(float dt) {
    if (slowMoTimer > 0.0f) slowMoTimer = std::max(0.0f, slowMoTimer - dt);
}

void PlayerAbilities::Bounce(float dx, float dy) {
    if (!bat) return;
    Vec2 p = bat->GetPosition();
    p.x += dx;
    p.y += dy;
    bat->SetPosition(p.x, p.y);
    // 击退/弹起是物理积分之外的直接位移，统一钳回窗口活动范围
    bat->ConstrainWithinWindow();
}

void PlayerAbilities::ActivateGiant() {
    if (!bat) return;
    float batSize = cfg_->GetBatSize();
    bat->SetScale(batSize * GiantScaleFactor, batSize * GiantScaleFactor);
    // 无敌/形态时长取设置页参数（原 GiantDuration 常量）。参数下限 1s：
    // 巨化形态与无敌共用同一个计时器，归零会让形态无法收尾（EndGiant 不再被触发）
    const float dur = tuning_->GiantInvulnSec();
    GrantInvincible(dur); // 巨化无敌经免费授予入口（不占 HUD 护盾计时）
    giantTimer = dur;
    profile_->AddCoins(5); // 巨化星额外奖励金币（B8）
    if (fx) {
        fx->PlaySFX((int)SFX::Record, 1.0f); // 稀有道具登场
        fx->FloatTextAt("巨化！", bat->GetPosition() + Vec2(0.0f, 80.0f),
                        0.95f, 0.85f, 0.4f, 30.0f, 1.5f);
        fx->Shake(8.0f);
    }
    std::cout << "Giant mode activated for " << dur << "s." << std::endl;
}

void PlayerAbilities::UpdateGiant(float dt) {
    if (giantTimer <= 0.0f || !bat) return;
    giantTimer -= dt;
    float batSize = cfg_->GetBatSize();

    if (giantTimer <= 0.0f) {
        giantTimer = 0.01f; // 保持活动语义，由 EndGiant 统一归零并还原体型/无敌
        EndGiant();
        return;
    }

    // 巨化期间保持 2 倍体型；最后 0.4s 平滑缩回
    float scale = batSize * GiantScaleFactor;
    if (giantTimer < 0.4f) {
        scale = batSize + (batSize * GiantScaleFactor - batSize) * (giantTimer / 0.4f);
    }
    bat->SetScale(scale, scale);
}

void PlayerAbilities::EndGiant() {
    if (giantTimer <= 0.0f || !bat) return;
    giantTimer = 0.0f;
    float batSize = cfg_->GetBatSize();
    bat->SetScale(batSize, batSize);
    if (invincibleTimer <= 0.0f)
        bat->SetInvincible(0.01f); // 巨化无敌随效果终止（无敌护盾在身时不清其免伤）
    if (fx)
        fx->FloatTextAt("巨化结束", bat->GetPosition() + Vec2(0.0f, 70.0f),
                        0.85f, 0.85f, 0.9f, 20.0f, 1.0f);
}

PlayerAbilities::GiantBody PlayerAbilities::Giant() const {
    GiantBody g;
    if (!bat || giantTimer <= 0.0f) return g;
    g.active = true;
    g.center = bat->GetPosition();
    g.halfSize = bat->GetScale().x * 0.5f;
    return g;
}

bool PlayerAbilities::IsInvincible() const {
    return HasOtherInvincibility() || shortShieldTimer > 0.0f;
}

bool PlayerAbilities::HasOtherInvincibility() const {
    // 无敌护盾/受击无敌/开局保护/巨化走 BatSprite 计时器（另有本地计时兜底）
    return bat && (bat->GetInvincibleState() || invincibleTimer > 0.0f || giantTimer > 0.0f);
}

float PlayerAbilities::TimeScale() const {
    if (slowMoTimer <= 0.0f) return 1.0f;
    const float k = std::clamp(slowMoTimer / SlowMoDur, 0.0f, 1.0f); // 1→0（钳制防外部更长请求致负缩放）
    return 1.0f - (1.0f - SlowMoScale) * k; // 触发瞬间 SlowMoScale，线性缓升回 1
}
