#include "scene/stage/WeatherDirector.h"
#include "gameplay/StageParams.h"
#include "core/Config.h"
#include "render/SpriteRenderer.h"
#include "render/RainRenderer.h"
#include "render/LightningRenderer.h"
#include "render/WindRenderer.h"
#include "res/ResourceManager.h"
#include "sprite/Sprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

// 天气消息盒顶 y：贴着 SCORE 栏下缘（+2px 间距），与 StageHud 的 SCORE 底板几何一致
float WeatherBoxTopY() {
    const ImGuiStyle& st = ImGui::GetStyle();
    constexpr float kScoreH = 56.0f;
    const float fpsBottom = 10.0f + st.WindowPadding.y * 2.0f + ImGui::GetTextLineHeight();
    return fpsBottom + 2.0f + kScoreH + 2.0f;
}

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

// 点到线段距离（用于蝙蝠圆 vs 雷折线各段的判定）
float PointSegDistance(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float abLenSq = ab.x * ab.x + ab.y * ab.y;
    float t = 0.0f;
    if (abLenSq > 1e-8f) {
        t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / abLenSq, 0.0f, 1.0f);
    }
    float dx = p.x - (a.x + ab.x * t);
    float dy = p.y - (a.y + ab.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

// 沿折线画半透明虚线（屏幕坐标）：按弧长交替 dashOn/dashOff
void DrawDashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col,
                        float thickness, float dashOn, float dashOff) {
    bool drawing = true;
    float budget = dashOn;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        ImVec2 a = pts[i], b = pts[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        ImVec2 dir(dx / len, dy / len);
        float s = 0.0f;
        while (s < len - 1e-4f) {
            float step = std::min(budget, len - s);
            if (drawing) {
                dl->AddLine(ImVec2(a.x + dir.x * s, a.y + dir.y * s),
                            ImVec2(a.x + dir.x * (s + step), a.y + dir.y * (s + step)),
                            col, thickness);
            }
            s += step;
            budget -= step;
            if (budget <= 1e-4f) {
                drawing = !drawing;
                budget = drawing ? dashOn : dashOff;
            }
        }
    }
}

} // namespace

void WeatherDirector::Init() {
    rainSystem = ResourceManager::Get().GetRainSystem();

    // ===== 渲染器注册表（方案一）：构造即注册，槽位分发表一次建立 =====
    auto rain = std::make_unique<RainRenderer>();
    auto lightning = std::make_unique<LightningRenderer>();
    auto wind = std::make_unique<WindRenderer>();
    rain->Init(rainSystem);
    lightning->Init(rainSystem);
    wind->Init(); // 风线（阵风可视化；数据自持，仅吃 Update 的阵风状态）

    windRenderer_ = wind.get(); // 非 owning：每帧喂阵风状态
    renderers_.push_back(std::move(rain));
    renderers_.push_back(std::move(lightning));
    renderers_.push_back(std::move(wind));

    for (auto& r : renderers_)
        for (WorldSlot s : r->Slots())
            slotOwners_[(size_t)s].push_back(r.get());

    // 天色变暗遮罩（A4-c）：白色纹理着色，透明度随难度档位渐变
    auto whiteTex = ResourceManager::Get().GetTexture2D("white");
    if (whiteTex) {
        stormScrimSprite = std::make_shared<Sprite>(whiteTex);
        stormScrimSprite->SetColor(0.05f, 0.06f, 0.13f, 0.0f);
        stormScrimSprite->SetScale(Config::GetInstance().GetScreenWidth() + 4.0f,
                                   Config::GetInstance().GetScreenHeight() + 4.0f);
        stormScrimSprite->SetPosition(Config::GetInstance().GetScreenWidth() * 0.5f,
                                      Config::GetInstance().GetScreenHeight() * 0.5f);
    }
}

void WeatherDirector::Reset() {
    msgs.clear();
    msgClock = 0.0f;
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
    // 阵风状态喂给风线渲染器（预警期无气流），再统一多态推进；
    // 雨/雷逻辑数据在 RainSystem 内，由 AdvanceRain 推进，不经此处
    windRenderer_->SetGust(gustTimer > 0.0f, gustForce);
    for (auto& r : renderers_) r->Update(dt);
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
void WeatherDirector::Post(int id, const std::string& text, float flashTime, bool autoSteady) {
    // 已存在同 id：刷新内容并重新置顶；否则插入到首位（最新消息排第一行）
    for (auto it = msgs.begin(); it != msgs.end(); ++it) {
        if (it->id == id) {
            Msg m = *it;
            m.text = text;
            m.warnTimer = flashTime;
            m.steady = false;
            m.autoSteady = autoSteady;
            msgs.erase(it);
            msgs.insert(msgs.begin(), m);
            return;
        }
    }
    Msg m;
    m.id = id;
    m.text = text;
    m.warnTimer = flashTime;
    m.autoSteady = autoSteady;
    msgs.insert(msgs.begin(), m);
    if (msgs.size() > MsgCap) msgs.resize(MsgCap);
}

void WeatherDirector::SetSteady(int id, const std::string& text) {
    for (auto& m : msgs) {
        if (m.id == id) {
            m.text = text;
            m.steady = true;
            m.warnTimer = 0.0f;
            return;
        }
    }
    // 不存在（跳过了预警阶段）：直接以常驻状态行置顶加入
    Msg m;
    m.id = id;
    m.text = text;
    m.steady = true;
    msgs.insert(msgs.begin(), m);
}

void WeatherDirector::Remove(int id) {
    for (auto it = msgs.begin(); it != msgs.end(); ++it) {
        if (it->id == id) {
            msgs.erase(it); // vector 抹除后，后续消息自动上移补位
            return;
        }
    }
}

void WeatherDirector::TickFeed(float dt) {
    msgClock += dt;

    // 预警闪烁计时：autoSteady 条目到点自转常驻；其余保持闪烁直到所属系统显式转换
    for (auto& m : msgs) {
        if (!m.steady && m.warnTimer > 0.0f) {
            m.warnTimer -= dt;
            if (m.warnTimer <= 0.0f) {
                if (m.autoSteady) m.steady = true;
                else m.warnTimer = 0.0f;
            }
        }
    }

    // ===== 雷击事件（轮询 RainSystem 状态机；预警开始前 2s 闪烁预报） =====
    if (rainSystem) {
        const auto phase = rainSystem->GetLightningPhase();
        bool exists = false, steady = false;
        for (const auto& m : msgs) {
            if (m.id == WMsgLightning) { exists = true; steady = m.steady; break; }
        }

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
    if (msgs.empty()) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 左上角盒：贴着 SCORE 栏下缘（高度可变，向下延伸）
    const float x0 = 24.0f;
    const float top = WeatherBoxTopY();
    const float w = 224.0f;
    const float headerH = 32.0f;
    const float lineH = 30.0f;
    const float h = headerH + lineH * (float)msgs.size() + 8.0f;

    // 底板整体提高透明度（文字与状态圆点除外），减少对游戏背景的遮挡
    UIBuilder::DrawRoundedRect(x0 + w * 0.5f, top + h * 0.5f, w, h, 14.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.45f),
        UITheme::ToImU32(UITheme::AmberDim, 0.55f), 2.0f);

    // 标题
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f + 1, top + 9.0f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), "天象预警");
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f, top + 9.0f),
                UITheme::ToImU32(UITheme::AmberDim), "天象预警");

    // 消息行：最新在第一行；预警行闪烁（红点 + 琥珀文字脉冲），状态行常驻（金点 + 月光白）
    for (size_t i = 0; i < msgs.size(); ++i) {
        const auto& m = msgs[i];
        const float cy = top + headerH + lineH * (float)i + lineH * 0.5f;
        ImVec2 ts = UIBuilder::CalcTextSize(m.text, 18.0f);

        if (m.steady) {
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Amber), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Moonlight, 0.92f), m.text.c_str());
        } else {
            float pulse = 0.5f + 0.5f * std::sin(msgClock * 10.0f); // 0~1 闪烁脉冲
            float a = 0.35f + 0.65f * pulse;
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Blood, 0.4f + 0.6f * pulse), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * a)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Amber, a), m.text.c_str());
        }
    }
}

bool WeatherDirector::CheckStrikeHit(Vec2 batPos) const {
    if (!rainSystem || !rainSystem->IsStrikeDangerous()) return false;
    // 精确判定：蝙蝠圆 vs 主分支折线。判定宽度逐段等于雷线视觉粗细
    // （含锥形收细，与 LightningRenderer 渲染公式一致），再加蝙蝠等效半径
    const auto& pts = rainSystem->GetLightning().mainBranch.points;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float halfW = std::max(rainSystem->GetMainBranchWidthAt(i),
                               rainSystem->GetMainBranchWidthAt(i + 1)) * 0.5f; // 段内取较宽端
        float d = PointSegDistance(batPos, pts[i], pts[i + 1]);
        if (d < halfW + LightningBatRadius) return true;
    }
    return false;
}

void WeatherDirector::DrawLightningWarning(float pulseClock) {
    if (!rainSystem) return;
    if (rainSystem->GetLightningPhase() != RainSystem::LightningPhase::Warning) return;

    const auto& pts = rainSystem->GetLightning().mainBranch.points;
    if (pts.size() < 2) return;

    const float SCREEN_WIDTH = (float)Config::GetInstance().GetScreenWidth();
    const float SCREEN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // 折线（世界坐标，y 向上）转屏幕坐标（y 向下）
    std::vector<ImVec2> screenPts;
    screenPts.reserve(pts.size());
    for (const auto& p : pts) {
        screenPts.emplace_back(p.x, SCREEN_HEIGHT - p.y);
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float pulse = 0.5f + 0.5f * std::sin(pulseClock * 14.0f);
    float progress = 1.0f - rainSystem->GetWarningTimer() / rainSystem->GetLightningWarnDuration(); // 0->1 越接近落雷越醒目

    // 1) 粗范围矩形：雷可能漂移的整个区域（落点 ±漂移钳制，全高），比虚线更淡
    float sx = rainSystem->GetStrikeX();
    float zoneHalf = RainSystem::StrikeDriftClamp;
    float rectAlpha = (0.08f + 0.14f * pulse) * (0.5f + 0.5f * progress);
    dl->AddRectFilledMultiColor(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha * 0.25f)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)),
        ImGui::GetColorU32(ImVec4(0.941f, 0.722f, 0.314f, rectAlpha)));
    dl->AddRect(
        ImVec2(sx - zoneHalf, 0.0f),
        ImVec2(sx + zoneHalf, SCREEN_HEIGHT),
        UITheme::ToImU32(UITheme::Amber, 0.25f + 0.35f * pulse), 4.0f, 0, 2.0f);

    // 2) 精确路径：沿主分支折线的半透明虚线（实际雷线将劈到的位置）
    float alpha = (0.30f + 0.25f * pulse) * (0.5f + 0.5f * progress);
    DrawDashedPolyline(dl, screenPts, UITheme::ToImU32(UITheme::Amber, alpha),
                       5.0f + 2.0f * progress, 16.0f, 11.0f);

    // 3) 落点标记：「雷」字（水平钳制在窗口内，避免越界）
    ImVec2 ts = UIBuilder::CalcTextSize("雷", 24.0f);
    float halfW = ts.x * 0.5f;
    float textX = std::clamp(sx, halfW + 8.0f, SCREEN_WIDTH - halfW - 8.0f);
    UIBuilder::DrawCenteredText("雷", textX,
                                SCREEN_HEIGHT - Config::GetInstance().GetTerrainHeight() - 22.0f,
                                24.0f, UITheme::ToImU32(UITheme::Amber, 0.5f + 0.5f * pulse));
}
