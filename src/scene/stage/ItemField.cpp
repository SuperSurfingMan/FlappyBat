#include "app/Services.h"
#include "scene/stage/ItemField.h"
#include "core/Config.h"
#include "render/SpriteRenderer.h"
#include "res/ResourceManager.h"
#include "sprite/Sprite.h"
#include "sprite/CollectableSprite.h"
#include "ui/UITheme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
    if (itemType < 0 || itemType >= (int)TypeCount) {
        throw std::out_of_range(
            "ItemField::EffectOf: 未知道具索引（检查 ItemCatalog 定义表）");
    }
    return ItemAt((size_t)itemType).effect;
}

void ItemField::Load() {
    for (int type = 0; type < (int)TypeCount; ++type) {
        pools_[type] = Services::Get().Res().GetSpriteList(ItemAt((size_t)type).poolKey);
    }
    giantOrb = Services::Get().Res().GetSprite("giantOrb");
}

void ItemField::SetPickupHandler(std::function<void(int)> cb) {
    onPickup_ = std::move(cb);
    for (int type = 0; type < (int)TypeCount; ++type) {
        for (auto& sp : *Pool(type)) {
            std::static_pointer_cast<CollectableSprite>(sp)->SetCallback([this, type]() {
                if (onPickup_) onPickup_(type);
            });
        }
    }
}

std::vector<std::shared_ptr<Sprite>>* ItemField::Pool(int type) {
    if (type < 0 || type >= (int)TypeCount) {
        throw std::out_of_range("ItemField::Pool: 未知道具类型");
    }
    return &pools_[type];
}

std::array<bool, ItemField::ItemPoolCap>* ItemField::Used(int type) {
    if (type < 0 || type >= (int)TypeCount) {
        throw std::out_of_range("ItemField::Used: 未知道具类型");
    }
    return &used_[type];
}

void ItemField::Reset() {
    for (int type = 0; type < (int)TypeCount; ++type) {
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
    for (int type = 0; type < (int)TypeCount; ++type) {
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
    for (int type = 0; type < (int)TypeCount; ++type) {
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
    for (int type = 0; type < (int)TypeCount; ++type) {
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
    if (type < 0 || type >= (int)TypeCount) return false;
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
    // 权重表驱动：新增道具只需在 ItemCatalog 加一行并给出两个权重，
    // 不再有绑定道具数量的阈值分支与手写失败回退链。
    const bool highTier = tier >= 5;
    float totalWeight = 0.0f;
    for (int type = 0; type < (int)TypeCount; ++type)
        totalWeight += ItemWeight((size_t)type, highTier);

    float roll = Random::Get().GetRandomFloat() * totalWeight;
    int chosen = (int)TypeCount - 1;
    for (int type = 0; type < (int)TypeCount; ++type) {
        const float w = ItemWeight((size_t)type, highTier);
        if (roll < w) { chosen = type; break; }
        roll -= w;
    }

    // 选中类型的对象池已满时依次尝试其它类型，保证走廊里仍有道具
    for (int k = 0; k < (int)TypeCount; ++k) {
        if (GenerateAt((chosen + k) % (int)TypeCount, position)) return;
    }
}
