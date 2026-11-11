#include "app/Services.h"
#include "enemy/owlking/OwlKing.h"
#include "sprite/Sprite.h"
#include "render/Texture2D.h"
#include "render/SpriteRenderer.h"
#include "core/Config.h"
#include "res/ResourceManager.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
// =============================================================================
// D14 血月枭王 · 表现层（第 4 期从 OwlKing.cpp 拆出）。
//
// 只放「看见什么」：序列帧帧序工具、动画状态机（期望动画推导 + 切表）、
// 世界层精灵绘制、前景层 HUD（血条/心数/声波槽/预警带/渐晕）。
// 任何玩法数值与相位判定都不在这里。
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
    auto tex = Services::Get().Res().GetTexture2D(ArtKey(c.key)); // 批次感知键（legacy/vector 同构表）
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

// ===== 世界层渲染（渲染期为表现路径，窗口尺寸直读 Config） =====

void OwlKing::RenderWorld(SpriteRenderer& renderer) {
    const float W = (float)Services::Get().Cfg().GetScreenWidth();
    const float H = (float)Services::Get().Cfg().GetScreenHeight();

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
    const float W = (float)Services::Get().Cfg().GetScreenWidth();
    const float H = (float)Services::Get().Cfg().GetScreenHeight();

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
        const float groundY = H - Services::Get().Cfg().GetTerrainHeight();
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

