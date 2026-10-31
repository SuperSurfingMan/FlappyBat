#pragma once
#include "scene/stage/StageFrame.h"
#include "scene/stage/RainSystem.h"
#include "render/IRenderer.h"
#include "render/WorldSlot.h"
#include "render/RenderContext.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

struct ModeParams;
class RainRenderer;
class LightningRenderer;
class WindRenderer;
class Sprite;

// 天气事件消息盒的固定槽位；怪物槽位自 IEnemy.h 的 kEnemyWeatherMsgSlotBase 起按 EnemyId 分段
enum WeatherMsgSlot { WMsgGust = 0, WMsgLightning = 1, WMsgStorm = 2 };

// =============================================================================
// 天气导演：阵风 / 落雷 / 天气事件消息盒 / 天色遮罩 / 雨强与滚动。
//
// 边界：
//   - 只读 StageFrame 与 ModeParams，不持有蝙蝠、不读场景字段；
//   - 阵风只计算推力向量（GustForce），由玩家子系统负责施加到蝙蝠速度；
//   - 雨/雷/风渲染器随天气数据一起下沉：以 IRenderer 注册表持有（方案一），
//     场景只经 RenderSlot 按 WorldSlot 槽位调用（方案二），不接触具体渲染器类型。
// =============================================================================
class WeatherDirector {
public:
    void Init();                                  // 取 RainSystem 并初始化三个渲染器、建槽位分发表
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
    void AdvanceRain(float dt); // 雨/雷逻辑数据在 RainSystem 内推进，渲染器不参与

    // 统一推进天气渲染器（方案一注册表）：先把阵风状态喂给风线渲染器，
    // 再经 IRenderer 注册表多态 Update（雨/雷渲染器为空实现）
    void UpdateFx(float dt);

    // 槽位渲染入口（方案二）：只画认领了该槽位的天气渲染器；
    // WeatherScrim 槽位额外叠加天色遮罩（普通 Sprite），其余槽位为空操作
    void RenderSlot(const RenderContext& ctx, WorldSlot slot);

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
    void RenderScrim(const RenderContext& ctx); // 天色变暗遮罩（透明度过低时为空操作）

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

    // ===== 雨/雷/风（方案一：渲染器注册表 + 槽位分发表）=====
    std::shared_ptr<RainSystem> rainSystem;
    std::vector<std::unique_ptr<IRenderer>> renderers_;
    // 槽位分发表：Init 时按各渲染器 Slots() 声明建立，渲染期零扫描
    std::array<std::vector<IRenderer*>, (size_t)WorldSlot::Count> slotOwners_;
    // 非 owning：指向注册表内的风线渲染器，每帧喂阵风状态用
    WindRenderer* windRenderer_ = nullptr;
};
