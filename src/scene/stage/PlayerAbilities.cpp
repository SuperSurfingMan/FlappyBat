#include "app/Services.h"
#include "scene/stage/PlayerAbilities.h"
#include "core/Config.h"
#include "sprite/BatSprite.h"
#include "audio/AudioSystem.h"
#include "app/PlayerProfile.h"
#include "enemy/IStageServices.h"
#include "ui/UITheme.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>

void PlayerAbilities::Init(std::shared_ptr<BatSprite> b, IFeedbackFx* feedback) {
    bat = std::move(b);
    fx = feedback;
}

void PlayerAbilities::Reset() {
    energy = 0.0f;
    shieldTimer = 0.0f;
    pulseClock = 0.0f;
    diveTimer = 0.0f;
    diveIframe = 0.0f;
    diveCooldown = 0.0f;
    giantTimer = 0.0f;
    slowMoTimer = 0.0f;
    locked_ = false;
}

void PlayerAbilities::Update(float dt, const StageFrame& f, Vec2 gustForce) {
    UpdateMotion(dt, f, gustForce);

    // ===== 能量护盾计时 =====
    pulseClock += dt;
    if (shieldTimer > 0.0f) shieldTimer = std::max(0.0f, shieldTimer - dt);
    if (f.input && f.input->Pressed(GLFW_KEY_SPACE)) {
        TryReleaseShield(); // 能量充满后按空格释放护盾
    }
}

void PlayerAbilities::UpdateMotion(float dt, const StageFrame& f, Vec2 gustForce) {
    if (!bat) return;

    // 阵风纵向分量：在积分前累加垂直速度（振翅重置速度=对抗手段）；
    // 横向分量不在此施加（输入每帧覆写横速，漂移语义由输入层负责）
    if (gustForce.y != 0.0f) {
        bat->SetVelocity(bat->GetVelocity() + gustForce.y * dt);
    }

    // 俯冲冷却随世界更新推进（死亡/受击不冻结；暂停/入场由场景门控自然冻结）
    if (diveCooldown > 0.0f) diveCooldown = std::max(0.0f, diveCooldown - dt);

    if (!bat->GetHitState()) {
        const bool diveRequested = f.input &&
            (f.input->Pressed(GLFW_KEY_S) || f.input->Pressed(GLFW_KEY_DOWN));

        // ===== 俯冲：S/↓ 触发定速急坠 0.18s（覆盖重力），全程无敌帧，冷却 2.5s =====
        if (diveTimer <= 0.0f && diveRequested && diveCooldown <= 0.0f && !IsGiant()) {
            diveTimer = DiveDuration;
            diveIframe = DiveInvulnDur;
            diveCooldown = DiveCooldownTime;
            if (!bat->GetInvincibleState())
                TryReleaseShield(DiveInvulnDur); // 无敌帧经统一入口授予（免费语义）
            if (fx) fx->PlaySFX((int)SFX::Fly, 0.6f);
        }
        if (diveIframe > 0.0f) diveIframe -= dt;
        if (diveTimer > 0.0f) {
            diveTimer -= dt;
            bat->SetVelocity(-DiveSpeed); // 定速急坠（GL y 向上为正；覆盖重力与振翅）
            bat->Falling(dt);             // 复用位置积分与窗口边界钳制（贴地俯冲被钳在地面线上）
        } else {
            bat->Falling(dt);
            // 鼠标左键 / 键盘（空格・W・↑）：均取已裁决快照，落在控件上的点击不会下发到玩法
            bool flapByMouse = f.input && f.input->PointerPressed(GLFW_MOUSE_BUTTON_LEFT);
            bool flapByKeyboard = f.input &&
                (f.input->Pressed(GLFW_KEY_SPACE) ||
                 f.input->Pressed(GLFW_KEY_W) ||
                 f.input->Pressed(GLFW_KEY_UP));
            if (flapByMouse || flapByKeyboard) {
                bat->JumpNow();
                if (fx) fx->PlaySFX((int)SFX::Fly, 0.8f);
            }
        }
        // A/D 或 ←/→ 持续按住 -> 横向移动（躲避全高雷柱；注意用 Held 持续语义）
        float hDir = 0.0f;
        if (f.input) {
            if (f.input->Held(GLFW_KEY_A) || f.input->Held(GLFW_KEY_LEFT)) hDir -= 1.0f;
            if (f.input->Held(GLFW_KEY_D) || f.input->Held(GLFW_KEY_RIGHT)) hDir += 1.0f;
        }
        bat->SetHorizontalSpeed(hDir * BatSprite::HorizontalMoveSpeed);
    }
    bat->Update(dt);
}

void PlayerAbilities::AddEnergy(float amount) {
    if (shieldTimer > 0.0f) return; // 护盾生效期间不积攒
    // B8：充能速度升级提升所有能量获取
    const float mult = 1.0f + ChargeMultPerLevel *
        (float)Services::Get().Profile().GetLevel(PlayerProfile::ShieldTrack::Charge);
    energy = std::min(1.0f, energy + amount * mult);
}

void PlayerAbilities::TryReleaseShield(float freeDuration) {
    if (!bat) return;
    if (freeDuration > 0.0f) {
        // 场景授予的免费无敌：不经能量闸门、不占护盾计时与 HUD、无护盾音效
        bat->SetInvincible(freeDuration);
        return;
    }
    if (locked_ || energy < 1.0f || shieldTimer > 0.0f) return;
    if (IsGiant()) return; // 巨化期间本身无敌，避免无敌计时被短护盾覆盖
    float dur = ShieldDuration(); // B8：持续时间随升级等级提升
    bat->SetInvincible(dur);
    shieldTimer = dur;
    energy = 0.0f;
    if (fx) fx->PlaySFX((int)SFX::Collect, 1.0f);
    std::cout << "Shield released! Invincible for " << dur << "s." << std::endl;
}

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
    // 击退/弹起是物理积分之外的直接位移（如俯冲撞击向左弹开 160px），
    // 统一钳回窗口活动范围，防止蝙蝠被顶出屏幕出现单帧消失
    bat->ConstrainWithinWindow();
}

void PlayerAbilities::ActivateGiant() {
    if (!bat) return;
    float batSize = Services::Get().Cfg().GetBatSize();
    bat->SetScale(batSize * GiantScaleFactor, batSize * GiantScaleFactor);
    TryReleaseShield(GiantDuration); // 巨化无敌经统一入口授予（免费语义）
    giantTimer = GiantDuration;
    Services::Get().Profile().AddCoins(5); // 巨化星额外奖励金币（B8）
    if (fx) {
        fx->PlaySFX((int)SFX::Record, 1.0f); // 稀有道具登场
        fx->FloatTextAt("巨化！", bat->GetPosition() + Vec2(0.0f, 80.0f),
                        0.95f, 0.85f, 0.4f, 30.0f, 1.5f);
        fx->Shake(8.0f);
    }
    std::cout << "Giant mode activated for " << GiantDuration << "s." << std::endl;
}

void PlayerAbilities::UpdateGiant(float dt) {
    if (giantTimer <= 0.0f || !bat) return;
    giantTimer -= dt;
    float batSize = Services::Get().Cfg().GetBatSize();

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
    float batSize = Services::Get().Cfg().GetBatSize();
    bat->SetScale(batSize, batSize);
    if (shieldTimer <= 0.0f)
        bat->SetInvincible(0.01f); // 巨化无敌随效果终止（护盾在身时不清护盾免伤）
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

float PlayerAbilities::ShieldDuration() const {
    int lv = Services::Get().Profile().GetLevel(PlayerProfile::ShieldTrack::Duration);
    return ShieldBaseDuration + ShieldDurationPerLevel * (float)lv;
}

bool PlayerAbilities::IsInvincible() const {
    return bat && (bat->GetInvincibleState() || shieldTimer > 0.0f || giantTimer > 0.0f);
}

float PlayerAbilities::TimeScale() const {
    if (slowMoTimer <= 0.0f) return 1.0f;
    const float k = std::clamp(slowMoTimer / SlowMoDur, 0.0f, 1.0f); // 1→0（钳制防外部更长请求致负缩放）
    return 1.0f - (1.0f - SlowMoScale) * k; // 触发瞬间 SlowMoScale，线性缓升回 1
}
