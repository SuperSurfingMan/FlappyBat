#pragma once
#include "StageFrame.h"
#include "../../Math.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

class Sprite;
class SpriteRenderer;

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
    enum ItemType { Mosquito = 0, Firefly = 1, Kiwi = 2, TypeCount = 3 };

    struct Effect {
        int   score;           // 直接加分（蚊子 +1，其余 0）
        float energy;          // 能量增量
        int   coins;           // 拾取金币
        const char* floatText; // 拾取飘字
        float r, g, b;
        float fontSize;
    };
    static const Effect& EffectOf(int itemType);

    void Load();
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
    static constexpr float EnergyPerKiwi = 1.0f / 3.0f;      // 3 个果实充满
    static constexpr float EnergyPerFirefly = 1.0f / 6.0f;   // 萤火虫充 1/6 能量
    static constexpr float ItemSpawnProbability = 0.55f;     // 每条走廊的生成概率
    static constexpr float ItemCorridorEdgeMargin = 90.0f;   // 距障碍物列中心的最小水平距离
    static constexpr float ItemMinCorridorWidth = 30.0f;     // 走廊可用宽度低于此值不生成
    static constexpr float ItemSpawnMinHeight = 90.0f;       // 距地面最小生成高度
    static constexpr float ItemSpawnTopMargin = 110.0f;      // 距天花板最小生成距离
    static constexpr float OrbCooldownTime = 20.0f;          // 巨化星生成冷却
    static constexpr float OrbSpawnChance = 0.10f;           // 巨化星生成概率
    static constexpr int   ItemPoolCap = 3;

    std::vector<std::shared_ptr<Sprite>> mosquitoes;
    std::vector<std::shared_ptr<Sprite>> fireflies;
    std::vector<std::shared_ptr<Sprite>> kiwis;
    std::array<bool, ItemPoolCap> mosquitoesUsed{};
    std::array<bool, ItemPoolCap> fireflyUsed{};
    std::array<bool, ItemPoolCap> kiwiUsed{};

    std::shared_ptr<Sprite> giantOrb;
    bool orbUsed = false;
    float orbCooldown = OrbCooldownTime;
    float orbBobTimer = 0.0f;

    std::function<void(int)> onPickup_;
    std::function<void()> onGiantOrb_;

    std::vector<std::shared_ptr<Sprite>>* Pool(int type);
    std::array<bool, ItemPoolCap>* Used(int type);
};
