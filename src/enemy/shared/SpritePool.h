#pragma once
#include "../../sprite/Sprite.h"
#include <memory>
#include <vector>

// =============================================================================
// 逻辑体 + 配套精灵的定长对象池（docs/怪物系统架构设计.md §4.5）。
// 怪物弹幕/道具池的标准容器：逻辑体 T 由怪物自定义（alive/pos/...），
// 精灵经工厂逐个创建（各自持独立动画/位置状态），下标与逻辑体一一对应。
// =============================================================================
template <typename T>
struct SpritePool {
    std::vector<T> items;                          // 逻辑体（alive 标志等）
    std::vector<std::shared_ptr<Sprite>> sprites;  // 渲染体（与 items 一一对应）

    template <typename F>
    void Init(size_t count, F&& makeSprite) {      // makeSprite() -> std::shared_ptr<Sprite>
        items.resize(count);
        sprites.clear();
        sprites.reserve(count);
        for (size_t i = 0; i < count; ++i) sprites.push_back(makeSprite());
    }
};
