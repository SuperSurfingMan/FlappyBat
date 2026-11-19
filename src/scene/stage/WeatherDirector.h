#pragma once
#include "scene/stage/StageFrame.h"
#include "scene/stage/RainSystem.h"
#include "scene/stage/WeatherMsgBox.h"
#include "render/IRenderer.h"
#include "render/WorldSlot.h"
#include "render/RenderContext.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

struct ModeParams;
class Config;
class ResourceManager;
class AudioSystem;
class TuningParams;
class RainRenderer;
class LightningRenderer;
class WindRenderer;
class Sprite;
struct LightningSense; // AutoPlay 感知快照（autoplay/AutoPilotSenses.h，填充在实现文件）

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
    // 依赖由场景注入：配置（遮罩几何/预警绘制）、资源（雨系统与白纹理）、
    // 音频（落雷音效经 RainSystem 的钩子上行）、调参（落雷频率倍率）。
    // 本类不再查全局服务。
    void Init(const Config& cfg, ResourceManager& res, AudioSystem& audio,
              const TuningParams& tuning);
    // 注册一个天气渲染器：立即按 Slots() 声明加入槽位分发表。
    // 新增特效 = 实现 IRenderer + 在 WeatherRendererRoster.cpp 加一行，本类零改动。
    void AddRenderer(std::unique_ptr<IRenderer> renderer);
    void Reset();                                 // 新一局：消息盒/阵风/遮罩归零
    void OnEnter(float lightningWarnLead);        // 进入场景：设置预警提前量

    // 消息盒计时 + 雷击事件轮询（不受暂停/入场冻结，保持氛围连续性）
    void TickFeed(float dt);

    // 阵风状态推进：结果写入 gustForce（仅生效期非零），由玩家子系统在蝙蝠积分前应用
    void UpdateGust(float dt, const StageFrame& f, const ModeParams& p);

    // 立即终止本轮阵风（死亡/超时结算调用）：清推力、计时与消息行。
    // 不清 gustCooldown——遭遇期暂停语义要求冷却冻结，新一局的冷却由 Reset 定。
    void StopGust();

    // 天色遮罩渐变 + 段落期雨强下限
    void UpdateAmbient(float dt, const StageFrame& f);

    void ApplyForTier(int tier, const ModeParams& p);

    void SetRainScrollSpeed(float v);
    void AdvanceRain(float dt); // 雨/雷逻辑数据在 RainSystem 内推进，渲染器不参与

    // 统一推进天气渲染器（方案一注册表）：先把阵风状态喂给风线渲染器，
    // 再经 IRenderer 注册表多态 Update（雨/雷渲染器为空实现）。
    // forceIdle=true 用于死亡动画/结算/入场等玩法冻结阶段：强制按"无阵风"下行，
    // 让上一局的风线正常淡出，而不是停在最后一帧不变。
    void UpdateFx(float dt, bool forceIdle = false);

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
    float GustRemaining() const { return gustTimer; } // 阵风生效剩余秒（AutoPlay 规划纳入外力）

    // ===== AutoPlay 感知（只读快照，从 RainSystem 既有公开接口转填）=====
    // 预警期雷折线已预生成且与落雷渲染/命中判定共用（"所见即所判"），
    // 观察者可在预警期精确推算雷线走廊
    void CaptureLightningSense(LightningSense& out) const;

private:
    void RenderScrim(const RenderContext& ctx); // 天色变暗遮罩（透明度过低时为空操作）

    // ===== 落雷调度：名义间隔到实际间隔的唯一换算处 =====
    // 模式参数/难度档位给的是"名义间隔"，实际间隔还要除设置页的雷击频率倍率；
    // 两个入口（OnEnter 起播 / ApplyForTier 改间隔）都经这里，调用点不再各写一遍换算
    Vec2 ScaledLightningInterval(float minSec, float maxSec) const;
    void ScheduleLightning(float minSec, float maxSec);

    // ===== 阵风（A4-b） =====
    static constexpr float GustDurationMin = 1.2f;
    static constexpr float GustDurationMax = 2.0f;
    static constexpr float GustForceMax = 220.0f;
    static constexpr float WeatherWarnLead = 2.0f;
    static constexpr float LightningBatRadius = 26.0f; // 蝙蝠等效受击半径（雷线走廊之外额外余量）

    float gustCooldown = 7.0f;
    float gustTimer = 0.0f;
    Vec2 gustForce{0.0f, 0.0f};        // 仅「生效期」非零；预警期必须为零，否则玩家会在无视觉提示时受力
    Vec2 gustPendingForce{0.0f, 0.0f}; // 预警期暂存的方向/力度，预警结束才转交 gustForce
    float gustWarnTimer = 0.0f;
    float gustPendingDuration = 0.0f;

    // ===== 消息盒（第 5 期抽成独立类：闪烁/常驻状态机与绘制几何自成一格）=====
    WeatherMsgBox msgBox_;

    // ===== 天色 / 雨强 =====
    std::shared_ptr<Sprite> stormScrimSprite;
    float scrimAlpha = 0.0f;
    float scrimTargetAlpha = 0.0f;
    float rainBaseline = 0.70f;

    const Config* cfg_ = nullptr; // 预警绘制与遮罩几何（Init 注入，生命周期长于本对象）
    const TuningParams* tuning_ = nullptr; // 设置页可改参数（落雷频率倍率；Init 注入）

    // ===== 雨/雷/风（方案一：渲染器注册表 + 槽位分发表）=====
    std::shared_ptr<RainSystem> rainSystem;
    std::vector<std::unique_ptr<IRenderer>> renderers_;
    // 槽位分发表：Init 时按各渲染器 Slots() 声明建立，渲染期零扫描
    std::array<std::vector<IRenderer*>, (size_t)WorldSlot::Count> slotOwners_;
};
