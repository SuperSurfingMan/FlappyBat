#pragma once
#include "StageFrame.h"
#include "../RainSystem.h"
#include "../../RainRenderer.h"
#include "../../LightningRenderer.h"
#include "../../WindRenderer.h"
#include <memory>
#include <string>
#include <vector>

struct ModeParams;

// 天气事件消息盒的固定槽位；怪物槽位自 IEnemy.h 的 kEnemyWeatherMsgSlotBase 起按 EnemyId 分段
enum WeatherMsgSlot { WMsgGust = 0, WMsgLightning = 1, WMsgStorm = 2 };

// =============================================================================
// 天气导演：阵风 / 落雷 / 天气事件消息盒 / 天色遮罩 / 雨强与滚动。
//
// 边界：
//   - 只读 StageFrame 与 ModeParams，不持有蝙蝠、不读场景字段；
//   - 阵风只计算推力向量（GustForce），由玩家子系统负责施加到蝙蝠速度；
//   - 雨/雷/风三个渲染器随天气数据一起下沉，场景只按世界深度槽位调用。
// =============================================================================
class WeatherDirector {
public:
    void Init();                                  // 取 RainSystem 并初始化三个渲染器
    void Reset();                                 // 新一局：消息盒/阵风/遮罩归零
    void OnEnter(float lightningWarnLead);        // 进入场景：设置预警提前量

    // 消息盒计时 + 雷击事件轮询（不受暂停/入场冻结，保持氛围连续性）
    void TickFeed(float dt);

    // 阵风状态推进：结果写入 gustForce，由玩家子系统在蝙蝠积分前应用
    void UpdateGust(float dt, const StageFrame& f, const ModeParams& p);

    // 天色遮罩渐变 + 段落期雨强下限
    void UpdateAmbient(float dt, const StageFrame& f);

    void ApplyForTier(int tier, const ModeParams& p);

    void SetRainScrollSpeed(float v);
    void AdvanceRain(float dt);
    void UpdateWind(float dt); // 风线可视化（仅气流生效期显示；预警期无气流）

    void RenderRain(SpriteRenderer& renderer, int depthLayer);
    void RenderLightning(SpriteRenderer& renderer);
    void RenderWind(SpriteRenderer& renderer);
    void RenderScrim(SpriteRenderer& renderer); // 天色变暗遮罩（透明度过低时为空操作）
    void DrawMsgBox();
    void DrawLightningWarning(float pulseClock);
    bool CheckStrikeHit(Vec2 batPos) const;

    // 消息盒：新增/刷新预警（置顶闪烁）、转常驻、移除
    void Post(int slot, const std::string& text, float flashTime, bool autoSteady);
    void SetSteady(int slot, const std::string& text);
    void Remove(int slot);

    Vec2 GustForce() const { return gustForce; }
    bool IsGustActive() const { return gustTimer > 0.0f; }
    bool IsGustWarning() const { return gustWarnTimer > 0.0f; }

private:
    struct Msg {
        int id = 0;
        std::string text;
        float warnTimer = 0.0f;
        bool steady = false;
        bool autoSteady = false;
    };

    // ===== 阵风（A4-b） =====
    static constexpr float GustDurationMin = 1.2f;
    static constexpr float GustDurationMax = 2.0f;
    static constexpr float GustForceMax = 220.0f;
    static constexpr float WeatherWarnLead = 2.0f;
    static constexpr size_t MsgCap = 4;
    static constexpr float LightningBatRadius = 26.0f; // 蝙蝠等效受击半径（雷线走廊之外额外余量）

    float gustCooldown = 7.0f;
    float gustTimer = 0.0f;
    Vec2 gustForce{0.0f, 0.0f};
    float gustWarnTimer = 0.0f;
    float gustPendingDuration = 0.0f;

    // ===== 消息盒 =====
    std::vector<Msg> msgs;
    float msgClock = 0.0f;

    // ===== 天色 / 雨强 =====
    std::shared_ptr<Sprite> stormScrimSprite;
    float scrimAlpha = 0.0f;
    float scrimTargetAlpha = 0.0f;
    float rainBaseline = 0.70f;

    // ===== 雨/雷/风 =====
    std::shared_ptr<RainSystem> rainSystem;
    RainRenderer rainRenderer;
    LightningRenderer lightningRenderer;
    WindRenderer windRenderer;
};
