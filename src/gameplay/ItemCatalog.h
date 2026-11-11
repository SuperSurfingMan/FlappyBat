#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

// =============================================================================
// 道具定义表（第 2 期）：新增道具的唯一登记点。
//
// 过去道具信息分散在四处：ItemField 的 ItemType 枚举 / EffectOf 表 / Pool()
// switch、ResourceManager 的贴图与精灵池注册、CollectableSprite 里
// `type == "kiwi"` 的帧数分支、GenItem 的两级概率阈值。
// 现在全部收进这一张 constexpr 表：新增道具 = 加一行定义 + 两张贴图文件，
// 下游（道具场 / 资源管理器 / 拾取结算）零改动。
// =============================================================================
struct ItemEffect {
    int         score;
    float       energy;
    int         coins;
    const char* floatText;
    float       r, g, b;      // 飘字颜色（与 UITheme 词汇一致）
    float       fontSize;
};

struct ItemDefinition {
    const char* key;             // 贴图键词干；消失动画键 = key + "Disappear"
    const char* poolKey;         // ResourceManager 精灵池键
    const char* texturePath;     // 常态序列帧
    const char* disappearPath;   // 拾取消散序列帧
    int         rows, cols;      // 常态序列帧网格
    int         disappearFrames; // 消散动画帧数
    ItemEffect  effect;
    float       weightNormal;    // 普通难度生成权重
    float       weightHighTier;  // 高难度（tier >= 5）生成权重
};

// 表长度由 CTAD 推导，不需要 TypeCount / 数量魔数
inline constexpr std::array kItemDefinitions = {
    ItemDefinition{
        "mosquitoe", "mosquitoePool",
        "assets/Animations/Spritesheets/mosquitoe.png",
        "assets/Animations/Spritesheets/mosquitoe_disappear.png",
        1, 8, 8,
        { 1, 0.0f,              1, "+1",     0.941f, 0.722f, 0.314f, 22.0f },
        0.50f, 0.45f },
    ItemDefinition{
        "firefly", "fireflyPool",
        "assets/Animations/Spritesheets/firefly.png",
        "assets/Animations/Spritesheets/firefly_disappear.png",
        1, 8, 8,
        { 0, 1.0f / 6.0f,      1, "能量+",  0.57f,  0.85f,  0.45f,  22.0f },
        0.40f, 0.40f },
    ItemDefinition{
        "kiwi", "kiwiPool",
        "assets/Animations/Spritesheets/kiwiFruit.png",
        "assets/Animations/Spritesheets/kiwi_disappear.png",
        2, 4, 6,
        { 0, 1.0f / 3.0f,      2, "能量++", 0.57f,  0.85f,  0.45f,  22.0f },
        0.10f, 0.15f },
};

inline constexpr std::size_t ItemCount() { return kItemDefinitions.size(); }

inline const ItemDefinition& ItemAt(std::size_t index) {
    if (index >= ItemCount())
        throw std::out_of_range("ItemAt: 道具索引越界");
    return kItemDefinitions[index];
}

inline float ItemWeight(std::size_t index, bool highTier) {
    const ItemDefinition& d = ItemAt(index);
    return highTier ? d.weightHighTier : d.weightNormal;
}
