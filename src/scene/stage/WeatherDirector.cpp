#include "scene/stage/WeatherDirector.h"
#include "gameplay/StageParams.h"
#include "core/Config.h"
#include "render/SpriteRenderer.h"
#include "scene/stage/WeatherRendererRoster.h"
#include "scene/stage/WeatherWarning.h"
#include "res/ResourceManager.h"
#include "audio/AudioSystem.h"
#include "sprite/Sprite.h"
#include <algorithm>

namespace {

// 四向阵风的消息文案（按推力向量取方向：仅一轴非零）
const char* GustWarnText(const Vec2& f) {
    if (f.y > 0.0f) return "上升气流来袭";
    if (f.y < 0.0f) return "下沉气流来袭";
    if (f.x < 0.0f) return "左向气流来袭";
    return "右向气流来袭";
}

const char* GustStateText(const Vec2& f) {
    if (f.y > 0.0f) return "上升气流";
    if (f.y < 0.0f) return "下沉气流";
    if (f.x < 0.0f) return "左向气流";
    return "右向气流";
}

} // namespace

void WeatherDirector::AddRenderer(std::unique_ptr<IRenderer> renderer) {
    if (!renderer) return;
    IRenderer* p = renderer.get();
    renderers_.push_back(std::move(renderer));
    // 按声明的槽位即时建表，运行期追加渲染器同样生效
    for (WorldSlot s : p->Slots())
        slotOwners_[(size_t)s].push_back(p);
}

void WeatherDirector::Init(const Config& cfg, ResourceManager& res, AudioSystem& audio) {
    cfg_ = &cfg;
    rainSystem = res.GetRainSystem();

    // 落雷音效：雨系统只负责"该响了"，具体发声由本导演装配（逻辑与音频解耦）
    if (rainSystem) {
        // 捕获指针而非引用变量：钩子会活到进程结束，不能依赖形参的作用域
        AudioSystem* pAudio = &audio;
        rainSystem->SetThunderHook([pAudio]() {
            pAudio->PlaySFX(SFXToFile(SFX::Thunder), 1.0f);
        });
    }

    // 渲染器由登记表注入：本导演不认识任何具体渲染器类型。
    // 新增特效 = 实现 IRenderer + 登记表加一行，本文件零改动。
    RegisterBuiltinWeatherRenderers(*this, rainSystem,
                                    (float)cfg.GetScreenWidth(), (float)cfg.GetScreenHeight());

    // 天色变暗遮罩（A4-c）：白色纹理着色，透明度随难度档位渐变
    auto whiteTex = res.GetTexture2D("white");
    if (whiteTex) {
        stormScrimSprite = std::make_shared<Sprite>(whiteTex);
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, 0.0f);
        stormScrimSprite->SetScale((float)cfg.GetScreenWidth() + 4.0f,
                                   (float)cfg.GetScreenHeight() + 4.0f);
        stormScrimSprite->SetPosition((float)cfg.GetScreenWidth() * 0.5f,
                                      (float)cfg.GetScreenHeight() * 0.5f);
    }
}

void WeatherDirector::Reset() {
    msgBox_.Reset();
    gustCooldown = 7.0f;
    gustTimer = 0.0f;
    gustForce = Vec2{0.0f, 0.0f};
    gustWarnTimer = 0.0f;
    gustPendingDuration = 0.0f;
    scrimAlpha = 0.0f;
    scrimTargetAlpha = 0.0f;
    rainBaseline = 0.70f;
    if (stormScrimSprite) stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, 0.0f);
    if (rainSystem) rainSystem->Reset();
}

void WeatherDirector::OnEnter(float lightningWarnLead) {
    if (!rainSystem) return;
    rainSystem->SetLightningWarnLead(lightningWarnLead);
    rainSystem->StartRandomLightning(5.0f, 10.0f);
}

void WeatherDirector::SetRainScrollSpeed(float v) {
    if (rainSystem) rainSystem->SetScrollSpeed(v);
}

void WeatherDirector::AdvanceRain(float dt) {
    if (rainSystem) rainSystem->Update(dt);
}

void WeatherDirector::UpdateFx(float dt) {
    // 帧数据经 IRenderer 默认虚方法统一下行（只有风线渲染器消费），
    // 再多态推进；雨/雷逻辑数据在 RainSystem 内，由 AdvanceRain 推进。
    const bool gustActive = gustTimer > 0.0f;
    for (auto& r : renderers_) {
        r->SetGust(gustActive, gustForce);
        r->Update(dt);
    }
}

void WeatherDirector::RenderSlot(const RenderContext& ctx, WorldSlot slot) {
    // 槽位分发表已按各渲染器 Slots() 声明建立：只画认领了该槽位的渲染器
    for (IRenderer* r : slotOwners_[(size_t)slot])
        r->Render(ctx, slot);

    // 天色遮罩是普通 Sprite（非 IRenderer）：跟随 WeatherScrim 槽位叠加
    if (slot == WorldSlot::WeatherScrim)
        RenderScrim(ctx);
}

void WeatherDirector::RenderScrim(const RenderContext& ctx) {
    if (stormScrimSprite && scrimAlpha > 0.001f) {
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, scrimAlpha);
        ctx.sprites.Draw_UV(stormScrimSprite);
    }
}

// ===== 阵风（A4-b） =====
void WeatherDirector::UpdateGust(float dt, const StageFrame& f, const ModeParams& p) {
    // 段落期（怪物接管）：阵风暂停（§9 避免与技能双重位移压力）；
    // 进行中/预警中的阵风立即取消并移除消息行，冷却冻结
    if (f.pauseGust) {
        if (gustTimer > 0.0f || gustWarnTimer > 0.0f) Remove(WMsgGust);
        gustTimer = 0.0f;
        gustWarnTimer = 0.0f;
        gustForce = Vec2{0.0f, 0.0f};
        return;
    }

    // 气流生效中：只维护推力向量与计时；纵向分量由玩家子系统在蝙蝠积分前施加
    if (gustTimer > 0.0f) {
        gustTimer = std::max(0.0f, gustTimer - dt);
        if (gustTimer <= 0.0f) {
            Remove(WMsgGust); // 状态结束：移除消息行（下方消息上移补位）
            gustForce = Vec2{0.0f, 0.0f};
        }
        return;
    }

    // 预警中（2s）：消息盒闪烁预报，尚无推力
    if (gustWarnTimer > 0.0f) {
        gustWarnTimer = std::max(0.0f, gustWarnTimer - dt);
        if (gustWarnTimer <= 0.0f) {
            // 预警结束 -> 气流生效：消息转常驻状态行
            gustTimer = gustPendingDuration;
            SetSteady(WMsgGust, GustStateText(gustForce));
        }
        return;
    }

    // 冷却到点：先决定气流参数并进入预警阶段（提前 2s 闪烁预报）
    gustCooldown -= dt;
    if (gustCooldown <= 0.0f) {
        // 四向等概率抽取：0 上 / 1 下 / 2 左 / 3 右（每次至多生效一种）
        int dirIdx = (int)(Random::Get().GetRandomFloat() * 4.0f);
        float strength = 0.6f + 0.4f * Random::Get().GetRandomFloat(); // 60%~100% 强度
        // 阵风力度与间隔来自模式参数表（风暴之夜更强更频繁，B9）
        float forceMax = GustForceMax * p.gustForceFactor;
        float mag = forceMax * strength;
        switch (dirIdx) {
            case 0:  gustForce = Vec2{0.0f,  mag}; break; // 上
            case 1:  gustForce = Vec2{0.0f, -mag}; break; // 下
            case 2:  gustForce = Vec2{-mag, 0.0f}; break; // 左
            default: gustForce = Vec2{ mag, 0.0f}; break; // 右
        }
        gustPendingDuration = GustDurationMin + (GustDurationMax - GustDurationMin) * Random::Get().GetRandomFloat();
        gustCooldown = p.gustIntervalMin +
                       (p.gustIntervalMax - p.gustIntervalMin) * Random::Get().GetRandomFloat();

        Post(WMsgGust, GustWarnText(gustForce), WeatherWarnLead, false);
        gustWarnTimer = WeatherWarnLead;
    }
}

// ===== 天色遮罩渐变 + 段落期雨强下限 =====
void WeatherDirector::UpdateAmbient(float dt, const StageFrame& f) {
    // 遮罩目标 = 档位基线 + 怪物段落压暗（scrimBoost，§9 +0.10）
    const float scrimTarget = std::clamp(scrimTargetAlpha + f.enemyScrimBoost, 0.0f, 1.0f);
    if (scrimAlpha != scrimTarget) {
        float step = dt * 0.25f;
        scrimAlpha += std::clamp(scrimTarget - scrimAlpha, -step, step);
    }

    // 段落期雨强下限（minRainIntensity，§9）：逐帧以 max(档位基线, 段落下限) 重设目标，
    // RainSystem 内部渐变，段落结束自然回落基线
    if (rainSystem) {
        rainSystem->SetRainIntensity(std::max(rainBaseline, f.enemyMinRain));
    }
    if (stormScrimSprite) {
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, scrimAlpha);
    }
}

// ===== 天气联动（A4-c） =====
void WeatherDirector::ApplyForTier(int tier, const ModeParams& p) {
    if (!rainSystem) return;

    // 风暴之夜（B9）：满雨量、密集落雷、天色直接压暗（常驻值来自模式参数表，覆盖档位曲线）
    if (p.stormWeather) {
        rainBaseline = p.stormRain;
        rainSystem->SetRainIntensity(p.stormRain);
        rainSystem->SetLightningIntervals(p.stormLightningMin, p.stormLightningMax);
        scrimTargetAlpha = p.stormScrim;
        return;
    }

    // 经典/限时：雨强/雷频/天色随难度档位渐变
    float t = std::clamp((float)tier / (float)kMaxDifficultyTier, 0.0f, 1.0f);
    // 雨强 70% -> 100%（基线留档：段落期雨强下限在其上合成，结束后回落）
    rainBaseline = 0.70f + 0.30f * t;
    rainSystem->SetRainIntensity(rainBaseline);
    // 落雷更频繁：5~10s -> 2.5~5s
    rainSystem->SetLightningIntervals(5.0f - 2.5f * t, 10.0f - 5.0f * t);
    // 天色自第 2 档起渐深，最深 0.32
    scrimTargetAlpha = 0.32f * std::clamp((tier - 1) / (float)(kMaxDifficultyTier - 1), 0.0f, 1.0f);
}

// ===== 天气事件消息盒 =====
// 状态机与绘制已迁至 WeatherMsgBox（第 5 期）；本类只保留"天气语义"的转发：
// 阵风/落雷/风暴档位何时播报、何时转常驻、何时移除由天气逻辑决定。

void WeatherDirector::Post(int id, const std::string& text, float flashTime, bool autoSteady) {
    msgBox_.Post(id, text, flashTime, autoSteady);
}

void WeatherDirector::SetSteady(int id, const std::string& text) {
    msgBox_.SetSteady(id, text);
}

void WeatherDirector::Remove(int id) {
    msgBox_.Remove(id);
}

void WeatherDirector::TickFeed(float dt) {
    msgBox_.Tick(dt);

    // ===== 雷击事件（轮询 RainSystem 状态机；预警开始前 2s 闪烁预报） =====
    if (rainSystem) {
        const auto phase = rainSystem->GetLightningPhase();
        bool steady = false;
        const bool exists = msgBox_.Exists(WMsgLightning, &steady);

        if (phase == RainSystem::LightningPhase::None) {
            if (exists && steady) {
                Remove(WMsgLightning); // 落雷威胁解除：消息移除、下方上移
            } else if (!exists) {
                float t = rainSystem->GetTimeToNextLightning();
                if (t >= 0.0f && t <= WeatherWarnLead) {
                    Post(WMsgLightning, "雷云聚集", WeatherWarnLead, false);
                }
            }
        } else {
            // 预警/落雷期间：消息转常驻「落雷预警」
            if (!steady) SetSteady(WMsgLightning, "落雷预警");
        }
    }
}

void WeatherDirector::DrawMsgBox() {
    msgBox_.Draw();
}

bool WeatherDirector::CheckStrikeHit(Vec2 batPos) const {
    if (!rainSystem || !rainSystem->IsStrikeDangerous()) return false;
    // 精确判定：蝙蝠圆 vs 主分支折线。判定宽度逐段等于雷线视觉粗细
    // （含锥形收细，与 LightningRenderer 渲染公式一致），再加蝙蝠等效半径
    const auto& pts = rainSystem->GetLightning().mainBranch.points;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float halfW = std::max(rainSystem->GetMainBranchWidthAt(i),
                               rainSystem->GetMainBranchWidthAt(i + 1)) * 0.5f; // 段内取较宽端
        float d = weather::DistancePointSegment(batPos, pts[i], pts[i + 1]);
        if (d < halfW + LightningBatRadius) return true;
    }
    return false;
}

void WeatherDirector::DrawLightningWarning(float pulseClock) {
    // 绘制细节在 WeatherWarning.cpp（预警走廊/虚线/落点标记三件套）。
    // 本类只负责"何时画"：预警相位才画，其余相位空操作由该函数自身兜住。
    if (!rainSystem || !cfg_) return;
    weather::DrawLightningWarningOverlay(*rainSystem, *cfg_, pulseClock);
}
