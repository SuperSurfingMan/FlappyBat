#pragma once
#include "scene/stage/StageFrame.h"
#include "gameplay/ItemCatalog.h"
#include "core/Math.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

class Sprite;
class SpriteRenderer;
class ResourceManager;
class AudioSystem;
class TuningParams;

// =============================================================================
// 道具场：蚊子 / 萤火虫 / 猕猴桃三类对象池 + 走廊生成策略 + 拾取判定 +
// 巨化星（稀有道具）的生成、飘移、炫彩与拾取。
//
// 边界：
//   - 只读 StageFrame；拾取收益通过 PickupHandler 交回场景结算（计分/能量/金币/连击/飘字）；
//   - 巨化星拾取通过回调通知玩家子系统激活巨化，道具不认识玩家状态机。
// =============================================================================
class ItemField {
public:
    // 道具种类、效果与生成权重统一由 gameplay/ItemCatalog.h 的定义表给出；
    // 新增道具不再需要扩枚举或改 EffectOf 表。
    static constexpr std::size_t TypeCount = ItemCount();
    using Effect = ItemEffect; // 兼容既有 ItemField::Effect 引用
    static const Effect& EffectOf(int itemType);

    // 资源、音频与调参由场景注入：道具池只认这三者，不查全局表
    // （调参提供"道具生产频率"倍率，生成概率本身仍是本类的常量基线）
    void Load(ResourceManager& res, AudioSystem& audio, const TuningParams& tuning);
    void Reset();
    void Update(float dt, const StageFrame& f);
    void Render(SpriteRenderer& renderer);
    void CheckPickup(Vec2 batPos, Vec2 batScale);
    void TrySpawnCorridor(float leftColumnX, float rightColumnX, const StageFrame& f);

    // 拾取回调：参数为 ItemType；收益由场景统一结算（模式倍率/成就/连击）
    void SetPickupHandler(std::function<void(int)> cb);
    void SetGiantOrbHandler(std::function<void()> cb) { onGiantOrb_ = std::move(cb); }

    bool IsOrbActive() const { return orbUsed; }

private:
    void GenItem(Vec2 position, int tier);
    bool GenerateAt(int type, Vec2 position);

    // ===== 生成参数 =====
    static constexpr float ItemSpawnProbability = 0.55f;     // 每条走廊的生成概率（基线，乘调参倍率后钳到 1）
    static constexpr float ItemCorridorEdgeMargin = 90.0f;   // 距障碍物列中心的最小水平距离
    static constexpr float ItemMinCorridorWidth = 30.0f;     // 走廊可用宽度低于此值不生成
    static constexpr float ItemSpawnMinHeight = 90.0f;       // 距地面最小生成高度
    static constexpr float ItemSpawnTopMargin = 110.0f;      // 距天花板最小生成距离
    static constexpr float OrbCooldownTime = 20.0f;          // 巨化星生成冷却
    static constexpr float OrbSpawnChance = 0.10f;           // 巨化星生成概率
    static constexpr int   ItemPoolCap = 3;

    // 两类并行容器统一按 ItemType 索引：新增道具只需加枚举值 + Load() 一行，
    // 不再有 Pool()/Used() 的 switch，也不会出现「default 静默落到猕猴桃池」的错乱。
    std::array<std::vector<std::shared_ptr<Sprite>>, TypeCount> pools_;
    std::array<std::array<bool, ItemPoolCap>, TypeCount> used_;

    std::shared_ptr<Sprite> giantOrb;
    bool orbUsed = false;
    float orbCooldown = OrbCooldownTime;
    float orbBobTimer = 0.0f;

    std::function<void(int)> onPickup_;
    std::function<void()> onGiantOrb_;

    AudioSystem* audio_ = nullptr; // 拾取音效（由 Load 注入；nullptr 时静音）
    const TuningParams* tuning_ = nullptr; // 调参（生成频率倍率；由 Load 注入）

    std::vector<std::shared_ptr<Sprite>>* Pool(int type);
    std::array<bool, ItemPoolCap>* Used(int type);
};
