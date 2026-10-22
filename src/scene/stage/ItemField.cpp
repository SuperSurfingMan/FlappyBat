#include "ItemField.h"
#include "../../Config.h"
#include "../../SpriteRenderer.h"
#include "../../res/ResourceManager.h"
#include "../../sprite/Sprite.h"
#include "../../sprite/CollectableSprite.h"
#include "../../ui/UITheme.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

// HSV(h∈[0,1), s, v) -> RGB：巨化星炫彩流转着色用
void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    int hi = (int)(h * 6.0f);
    float f = h * 6.0f - (float)hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (hi % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

} // namespace

const ItemField::Effect& ItemField::EffectOf(int itemType) {
    static const Effect table[TypeCount] = {
        /* 蚊子   */ { 1, 0.0f,              1, "+1",     UITheme::Amber.r, UITheme::Amber.g, UITheme::Amber.b, 22.0f },
        /* 萤火虫 */ { 0, EnergyPerFirefly,  1, "能量+",  0.57f,            0.85f,            0.45f,            22.0f },
        /* 猕猴桃 */ { 0, EnergyPerKiwi,     2, "能量++", 0.57f,            0.85f,            0.45f,            22.0f },
    };
    return table[itemType];
}

void ItemField::Load() {
    mosquitoes = ResourceManager::Get().GetSpriteList("mosquitoePool");
    fireflies = ResourceManager::Get().GetSpriteList("fireflyPool");
    kiwis = ResourceManager::Get().GetSpriteList("kiwiPool");
    giantOrb = ResourceManager::Get().GetSprite("giantOrb");
}

void ItemField::SetPickupHandler(std::function<void(int)> cb) {
    onPickup_ = std::move(cb);
    for (int type = 0; type < TypeCount; ++type) {
        for (auto& sp : *Pool(type)) {
            std::static_pointer_cast<CollectableSprite>(sp)->SetCallback([this, type]() {
                if (onPickup_) onPickup_(type);
            });
        }
    }
}

std::vector<std::shared_ptr<Sprite>>* ItemField::Pool(int type) {
    switch (type) {
        case Mosquito: return &mosquitoes;
        case Firefly:  return &fireflies;
        default:       return &kiwis;
    }
}

std::array<bool, ItemField::ItemPoolCap>* ItemField::Used(int type) {
    switch (type) {
        case Mosquito: return &mosquitoesUsed;
        case Firefly:  return &fireflyUsed;
        default:       return &kiwiUsed;
    }
}

void ItemField::Reset() {
    for (int type = 0; type < TypeCount; ++type) {
        Used(type)->fill(false);
        for (auto& sp : *Pool(type)) sp->Reset();
    }
    if (giantOrb) {
        giantOrb->Reset();
        giantOrb->setActive(false);
    }
    orbUsed = false;
    orbCooldown = OrbCooldownTime;
    orbBobTimer = 0.0f;
}

void ItemField::Update(float dt, const StageFrame& f) {
    const float move = f.obstacleSpeed * dt; // 与障碍物同速，保持相对位置
    const int leftBorder = -25;

    // 三类道具统一处理：随世界左移 -> 动画推进 -> 出界释放回对象池
    for (int type = 0; type < TypeCount; ++type) {
        auto& pool = *Pool(type);
        auto& used = *Used(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (!used[i]) continue;
            const auto& pos = pool[i]->GetPosition();
            pool[i]->SetPosition(pos.x - move, pos.y);
            pool[i]->Update(dt);
            if (pool[i]->GetPosition().x < leftBorder) {
                used[i] = false;
                pool[i]->Reset();
            }
        }
    }

    // ===== 巨化星：冷却 / 左移 / 炫彩 / 出界 / 拾取 =====
    orbCooldown = std::max(0.0f, orbCooldown - dt);
    if (!orbUsed || !giantOrb) return;

    orbBobTimer += dt;
    const auto& pos = giantOrb->GetPosition();
    giantOrb->SetPosition(pos.x - f.obstacleSpeed * dt, pos.y);
    giantOrb->Update(dt);

    {
        float r, g, b;
        HsvToRgb(0.11f + orbBobTimer * 0.4f, 0.9f, 1.0f, r, g, b); // 0.11 = 金色起点
        giantOrb->SetColor(r, g, b, 1.0f);
    }
    giantOrb->SetGlow(0.9f + 0.7f * std::sin(orbBobTimer * 6.0f)); // 发光脉冲

    if (giantOrb->GetPosition().x < -40.0f) {
        orbUsed = false;
        giantOrb->setActive(false);
        return;
    }

    // 拾取判定（AABB，与 CollectableSprite 同思路；巨化中的大碰撞盒也更容易吃到）
    const Vec2& batPos = f.batPos;
    const Vec2& batScale = f.batScale;
    const Vec2& orbScale = giantOrb->GetScale();
    const Vec2& cur = giantOrb->GetPosition();
    if (std::abs(cur.x - batPos.x) < (orbScale.x + batScale.x) * 0.5f &&
        std::abs(cur.y - batPos.y) < (orbScale.y + batScale.y) * 0.5f) {
        orbUsed = false;
        giantOrb->setActive(false);
        if (onGiantOrb_) onGiantOrb_();
    }
}

void ItemField::Render(SpriteRenderer& renderer) {
    for (int type = 0; type < TypeCount; ++type) {
        auto& pool = *Pool(type);
        auto& used = *Used(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (used[i]) renderer.Draw_UV(pool[i]);
        }
    }
    if (giantOrb && orbUsed) {
        renderer.Draw_UV(giantOrb);
    }
}

void ItemField::CheckPickup(Vec2 batPos, Vec2 batScale) {
    // 三类道具统一轮询（碰撞命中时由 CollectableSprite 内部触发拾取回调）
    for (int type = 0; type < TypeCount; ++type) {
        auto& pool = *Pool(type);
        auto& used = *Used(type);
        for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
            if (!used[i]) continue;
            std::static_pointer_cast<CollectableSprite>(pool[i])->CheckCollision(batPos, batScale);
        }
    }
}

void ItemField::TrySpawnCorridor(float leftColumnX, float rightColumnX, const StageFrame& f) {
    // 段落期（怪物接管）：停止走廊道具生成（玩家专注攻击/闪避；怪物道具接管补给）
    if (f.suppressItemSpawn) return;

    // 巨化星（最稀有道具）：低概率 + 冷却 + 同屏至多一颗
    if (giantOrb && !orbUsed && !f.batGiant && orbCooldown <= 0.0f &&
        Random::Get().GetRandomFloat() < OrbSpawnChance) {
        float minXo = leftColumnX + ItemCorridorEdgeMargin;
        float maxXo = rightColumnX - ItemCorridorEdgeMargin;
        float x = minXo + (maxXo - minXo) * Random::Get().GetRandomFloat();
        float minY = f.terrainY + ItemSpawnMinHeight;
        float maxY = f.screenH - ItemSpawnTopMargin;
        float y = minY + (maxY - minY) * Random::Get().GetRandomFloat();
        giantOrb->SetPosition(x, y);
        giantOrb->setActive(true);
        orbUsed = true;
        orbCooldown = OrbCooldownTime;
        std::cout << "Giant star orb spawned." << std::endl;
        return;
    }

    if (Random::Get().GetRandomFloat() > ItemSpawnProbability) return;

    float minX = leftColumnX + ItemCorridorEdgeMargin;
    float maxX = rightColumnX - ItemCorridorEdgeMargin;
    if (maxX - minX < ItemMinCorridorWidth) return; // 走廊太窄，放弃生成

    float x = minX + (maxX - minX) * Random::Get().GetRandomFloat();
    float minY = f.terrainY + ItemSpawnMinHeight;
    float maxY = f.screenH - ItemSpawnTopMargin;
    float y = minY + (maxY - minY) * Random::Get().GetRandomFloat();

    GenItem(Vec2{x, y}, f.tier);
}

bool ItemField::GenerateAt(int type, Vec2 position) {
    if (type < 0 || type >= TypeCount) return false;
    auto& pool = *Pool(type);
    auto& used = *Used(type);
    for (size_t i = 0; i < pool.size() && i < used.size(); ++i) {
        if (used[i]) continue;
        pool[i]->SetPosition(position.x, position.y);
        used[i] = true;
        return true;
    }
    return false; // 该类型对象池已满
}

void ItemField::GenItem(Vec2 position, int tier) {
    float indicator = Random::Get().GetRandomFloat();
    // 高难度阶段提升能量道具占比（风险对冲）：kiwi 10%->15%，蚊子 50%->45%
    bool highTier = tier >= 5;
    float kiwiThreshold = highTier ? 0.85f : 0.90f;
    float fireflyThreshold = highTier ? 0.45f : 0.50f;
    if (indicator > kiwiThreshold) {
        if (!GenerateAt(Kiwi, position)) {
            if (!GenerateAt(Firefly, position)) GenerateAt(Mosquito, position);
        }
    } else if (indicator > fireflyThreshold) {
        if (!GenerateAt(Firefly, position)) GenerateAt(Mosquito, position);
    } else {
        GenerateAt(Mosquito, position);
    }
}
