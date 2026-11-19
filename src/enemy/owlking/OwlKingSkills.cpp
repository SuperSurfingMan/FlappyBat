#include "app/Services.h"
#include "enemy/owlking/OwlKing.h"
#include "sprite/Sprite.h"
#include "render/SpriteRenderer.h"
#include "core/Config.h"
#include "audio/AudioSystem.h"
#include "enemy/IStageServices.h"
#include "gameplay/ShieldRules.h"

#include <algorithm>
#include <cmath>
// =============================================================================
// D14 血月枭王 · 技能循环、弹幕与判定（第 4 期从 OwlKing.cpp 拆出）。
//
// 只放「打什么、怎么飞、打中了算什么」：技能起手/预警/释放/力竭推进、
// 弹体与水晶池、双向伤害结算。相位切换与逐帧编排仍在 OwlKing.cpp，
// 世界层与前景层绘制在 OwlKingRender.cpp。
// =============================================================================

// ===== 技能各状态时长表（UpdateSkill 推进与 CaptureSense 威胁快照共用）=====

float OwlKing::TelegraphDur(Skill s) {
    switch (s) {
        case Skill::FeatherStorm: return 0.8f;
        case Skill::MoonGaze:     return 1.2f;
        case Skill::ShadowDash:   return 1.2f;
        case Skill::ScreechWave:  return 1.0f;
        case Skill::StormCall:    return 1.0f;
        default:                  return 1.0f; // BloodRain
    }
}

float OwlKing::ActiveDur() const {
    switch (curSkill) {
        case Skill::MoonGaze:    return 0.6f + 240.f / 120.f; // 静止 + 扫掠（同 UpdateSkill 推进公式）
        case Skill::ShadowDash:  return ShadowDashDur;
        case Skill::ScreechWave: return ScreechActiveDur;
        case Skill::StormCall:   return StormCallActiveDur;
        case Skill::BloodRain:   return BloodRainActiveDur;
        default:                 return 0.f; // FeatherStorm 即发即竭
    }
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
        // 三道音波的缺口起手即定且近似对齐：同一随机基准高度 ±12px 逐道抖动，
        // 玩家一次就位即可依次穿过全部弧线（缺口高 72px = 蝙蝠碰撞直径 64 + 8px 余量）
        const float lo = ctx.terrainY + 110.f;
        const float hi = std::max(lo + 30.f, ctx.screenH - 120.f);
        const float base = lo + (hi - lo) * Random::Get().GetRandomFloat();
        for (auto& w : screeches) {
            w.alive = false;
            w.age = 0.f;
            w.centerY = bossY;
            w.gapY = std::clamp(base + (Random::Get().GetRandomFloat() - 0.5f) * 2.f * ScreechGapJitter,
                                lo, hi);
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
    ctx.fxFx->PlaySFX((int)SFX::Countdown, 1.0f);
    // 消息盒技能预警（§5：B1「羽刃来袭」/ B4「枭啸蓄力」；独立槽位避免顶掉常驻枭王行）
    if (s == Skill::FeatherStorm) ctx.fxFx->PostWeatherMsg(SkillMsgSlot(), "羽刃来袭", 0.8f, false);
    if (s == Skill::ScreechWave) ctx.fxFx->PostWeatherMsg(SkillMsgSlot(), "枭啸蓄力", 1.0f, false);
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
            const float teleDur = TelegraphDur(curSkill); // 预警时长表（与威胁快照共用）
            if (skillTimer >= teleDur) {
                skillState = SkillState::Active;
                skillTimer = 0.f;
                ctx.fxFx->RemoveWeatherMsg(SkillMsgSlot()); // 技能预警闪现结束（B1/B4；其余走自绘预警）
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
                        f.parried = false;     // 对象池复用：清掉上一发的弹反/挂起状态
                        f.pendingAge = -1.f;
                        f.alive = true;
                        if (++fired >= n) break;
                    }
                    // 羽刃即刻倾泻完毕 → 直接进入力竭
                    skillState = SkillState::Fatigue;
                    skillTimer = 0.f;
                    ctx.fxFx->PlaySFX((int)SFX::Thunder, 1.0f);
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
                // （预警虚线由本侧自绘先行展示；落雷经 IFeedbackFx 收口，
                //   场景转发到雨系统管线：危险窗/音效/渲染复用既有路径）
                IFeedbackFx& fx = *ctx_->fxFx;
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
                                d.parried = false;      // 对象池复用
                                d.pendingAge = -1.f;
                                d.vel = Vec2{0.f, -BloodDropSpeed};
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

    // ===== B1 羽刃：碰撞挂起 -> 弹反/受击结算 -> 弹回反伤本体 -> 出界回收 =====
    // 与短护盾的约定（gameplay/ShieldRules.h）：子弹在 t 与蝙蝠判定碰撞时先挂起
    // kParryGrace 秒；期间玩家释放短护盾（ctx.parryArmed）即额外触发弹反，
    // 子弹重算轨迹飞向本体；超时才按正常受击结算——这就是 [t, t+0.1] 窗口。
    for (auto& f : feathers.items) {
        if (!f.alive) continue;

        // (a) 挂起中：子弹冻结，等待弹反补救输入
        if (f.pendingAge >= 0.f) {
            f.pendingAge += dt;
            if (ctx.parryArmed) {
                f.pendingAge = -1.f;
                f.parried = true;
                f.vel = VelocityToBoss(f.pos);
                OnParrySuccess(ctx, f.pos);
                continue;
            }
            if (f.pendingAge > shield::kParryGrace) {
                f.pendingAge = -1.f;
                DamageBat(ctx, 0.f); // 窗口超时：正常受击
                f.alive = false;
            }
            continue;
        }

        // 弹反后的子弹持续重新瞄准本体当前位置：本体在浮游，保证「飞向枭王」真能命中
        if (f.parried) f.vel = VelocityToBoss(f.pos);
        f.pos += Vec2{f.vel.x * dt, f.vel.y * dt};
        const float dx = f.pos.x - ctx.batPos.x;
        const float dy = f.pos.y - ctx.batPos.y;

        // (b) 与蝙蝠判定碰撞
        if (!f.parried && dx * dx + dy * dy < (14.f + batR) * (14.f + batR)) {
            if (ctx.parryArmed) {
                // 碰撞当帧（t）就放出短护盾：直接弹反
                f.parried = true;
                f.vel = VelocityToBoss(f.pos);
                OnParrySuccess(ctx, f.pos);
            } else if (ctx.batOtherInvincible) {
                f.alive = false;               // 原有无敌（无敌护盾/巨化等）吸收，不碎短护盾
            } else if (ctx.batShieldActive) {
                ctx.fxPlayer->ConsumeShortShield(); // 短护盾已生效：挡下一次即消失（不弹反）
                f.alive = false;
            } else if (ctx.batInvincible) {
                f.alive = false;               // 聚合兜底：本帧稍早已碎盾
            } else {
                f.pendingAge = 0.f;           // 无防护：挂起，等待 0.1s 内补放短护盾
            }
            continue;
        }
        // (c) 弹回的羽刃进入本体包围盒（与音爆同规则，含纵向判定）
        if (f.parried && f.pos.x >= BossPosX - BossHitHalfW &&
            std::fabs(f.pos.y - bossY) < BossH * 0.5f) {
            DamageBoss(ParryDamage, f.pos);
            f.alive = false;
            continue;
        }
        if (f.pos.x < -30.f || f.pos.x > ctx.screenW + 40.f ||
            f.pos.y < -30.f || f.pos.y > ctx.screenH + 30.f) {
            f.alive = false;
        }
    }

    // ===== B6 坠羽：与羽刃同一套挂起/弹反规则 =====
    for (auto& d : bloodDrops) {
        if (!d.alive) continue;

        if (d.pendingAge >= 0.f) {
            d.pendingAge += dt;
            if (ctx.parryArmed) {
                d.pendingAge = -1.f;
                d.parried = true;
                d.vel = VelocityToBoss(d.pos);
                OnParrySuccess(ctx, d.pos);
                continue;
            }
            if (d.pendingAge > shield::kParryGrace) {
                d.pendingAge = -1.f;
                DamageBat(ctx, 0.f);
                d.alive = false;
            }
            continue;
        }

        if (d.parried) d.vel = VelocityToBoss(d.pos);
        d.pos += Vec2{d.vel.x * dt, d.vel.y * dt};
        const float dx = d.pos.x - ctx.batPos.x;
        const float dy = d.pos.y - ctx.batPos.y;
        if (!d.parried && dx * dx + dy * dy < (12.f + batR) * (12.f + batR)) {
            if (ctx.parryArmed) {
                d.parried = true;
                d.vel = VelocityToBoss(d.pos);
                OnParrySuccess(ctx, d.pos);
            } else if (ctx.batOtherInvincible) {
                d.alive = false;
            } else if (ctx.batShieldActive) {
                ctx.fxPlayer->ConsumeShortShield();
                d.alive = false;
            } else if (ctx.batInvincible) {
                d.alive = false;
            } else {
                d.pendingAge = 0.f;
            }
            continue;
        }
        if (d.parried && d.pos.x >= BossPosX - BossHitHalfW &&
            std::fabs(d.pos.y - bossY) < BossH * 0.5f) {
            DamageBoss(ParryDamage, d.pos);
            d.alive = false;
            continue;
        }
        // 未弹反的坠羽落地回收；弹反后改按通用出界回收
        if (!d.parried && d.pos.y < ctx.terrainY - 10.f) d.alive = false;
        if (d.parried && (d.pos.x < -30.f || d.pos.x > ctx.screenW + 40.f ||
                          d.pos.y < -30.f || d.pos.y > ctx.screenH + 60.f)) {
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

    // 俯冲撞击（B6-a）随下坠冲刺机制一并取消；弹反输出改由短护盾弹回子弹承担。

    // 巨化冲撞（§4.5 二期调整）：巨化蝙蝠任意方向撞上本体 → 20 伤害，撞击一次后巨化立即结束
    // （ctx.batRadius 已按巨化体型放大；交战期巨化本就免伤，撞击是巨化期的唯一输出手段）
    if (ctx.batGiant &&
        std::fabs(ctx.batPos.x - BossPosX) < BossHitHalfW + ctx.batRadius &&
        std::fabs(ctx.batPos.y - bossY) < BossH * 0.5f + ctx.batRadius) {
        DamageBoss(GiantHitDamage, ctx.batPos);
        ctx.fxFx->Shake(8.0f);
        ctx.fxPlayer->EndGiant(); // 撞击一次后巨化立刻消失
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
            DamageBat(ctx, 0.f); // 缺口（高 72px = 碰撞直径 64+8 余量，另加半幅中心宽容）内穿越则安全
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
            IFeedbackFx& fx = *ctx_->fxFx;
            IRunEconomy& eco = *ctx_->fxEconomy;
            IPlayerCommands& pl = *ctx_->fxPlayer;
            eco.AddCoins(CrystalCoin); // 顺带金币（§4.2；对齐巨化星惯例，不另发飘字）
            fx.PlaySFX((int)SFX::Collect, 1.0f);
            fx.FloatTextAt("声波 +1", c.pos + Vec2{0.f, 34.f},
                         0.57f, 0.85f, 0.95f, 20.f, 1.0f);
            pl.OnEnemyItemCollected(c.pos); // 预留：连击/成就通道（一期空操作）
        }
    }
}

void OwlKing::DamageBat(const EnemyContext& ctx, float edgeY) {
    // 伤害闸门顺序（与场景碰撞分支一致）：
    //   1) 其它无敌（无敌护盾/巨化/开局保护/受击无敌）直接吸收，不消耗短护盾；
    //   2) 本侧帧内免伤门（hitInvTimer）优先于短护盾，避免同一波弹幕白白碎盾；
    //   3) 一次性短护盾挡下一次即碎；
    //   4) 都没有才扣心。
    if (ctx.batOtherInvincible) return;
    if (hitInvTimer > 0.f) return;
    if (ctx.batShieldActive) {
        if (ctx.fxPlayer && ctx.fxPlayer->ConsumeShortShield())
            hitInvTimer = std::max(hitInvTimer, shield::kBreakInvuln);
        return;
    }
    if (ctx.batInvincible) return; // 聚合兜底：本帧稍早已碎盾/无敌
    if (batHearts <= 0) return;
    batHearts--;
    hitInvTimer = HitInvulnDur; // 本侧帧内精确免伤门（蝙蝠侧闪烁无敌经 GrantBatInvuln 另行授予）
    // 受击反馈（§6/§8）：音效/抖屏 10/飘字/无敌闪烁/击退/心形碎裂
    IFeedbackFx& fx = *ctx_->fxFx;
    IPlayerCommands& pl = *ctx_->fxPlayer;
    fx.PlaySFX((int)SFX::Death, 1.0f);
    fx.Shake(10.0f);                                   // §8 抖动表：受击 10
    fx.FloatTextAt("受击 -1", ctx.batPos + Vec2{0.f, 60.f},
                 0.87f, 0.35f, 0.35f, 24.f, 1.0f);
    pl.GrantBatInvuln(HitInvulnDur);                   // §6：1.2s 受击无敌（蝙蝠辉光闪烁反馈）
    if (edgeY != 0.f) {
        pl.BounceBat(0.f, (edgeY < 0.f) ? 90.f : -90.f); // 撞地弹起 / 撞顶下压
    } else {
        pl.BounceBat(-KnockBackX, 0.f);                  // §6：技能命中向左击退 100px
    }
    heartBreakIdx = batHearts;                           // §8：刚被击碎的心形槽位（碎裂残影）
    heartBreakTimer = HeartBreakDur;
    if (batHearts <= 0) pendingKill_ = true; // 心数耗尽 -> Update 收尾统一致死
}

Vec2 OwlKing::VelocityToBoss(Vec2 from) const {
    // 重算轨迹：始终指向本体当前中心（本体纵向浮游，UpdateProjectiles 每帧重瞄）
    const Vec2 d{BossPosX - from.x, bossY - from.y};
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1.f) return Vec2{ParrySpeed, 0.f};
    return Vec2{d.x / len * ParrySpeed, d.y / len * ParrySpeed};
}

void OwlKing::OnParrySuccess(const EnemyContext& ctx, Vec2 at) {
    // 弹反也算「挡下一次受击」：短护盾随即消失（CD 已在释放时重置，不受影响）
    if (ctx.fxPlayer && ctx.fxPlayer->ConsumeShortShield())
        hitInvTimer = std::max(hitInvTimer, shield::kBreakInvuln);
    if (ctx.fxFx) {
        ctx.fxFx->PlaySFX((int)SFX::Point, 1.0f);
        ctx.fxFx->FloatTextAt("弹反！", at + Vec2{0.f, 26.f},
                             1.f, 0.95f, 0.55f, 20.f, 0.8f);
        ctx.fxFx->Shake(5.0f);
    }
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
    IFeedbackFx& fx = *ctx_->fxFx;
    fx.PlaySFX((int)SFX::Point, 1.0f);
    fx.Shake(6.0f);                                    // §8 抖动表：命中 6
    fx.FloatTextAt("-" + std::to_string(dmg), at + Vec2{0.f, 40.f},
                 0.95f, 0.82f, 0.35f, 22.f, 1.0f);
    if (hp <= 0) {
        hp = 0;
        EnterDefeat();
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

