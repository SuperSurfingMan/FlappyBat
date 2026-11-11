#pragma once
#include <cstddef>

// =============================================================================
// 主菜单布局常量与几何推导（第 5 期从 ReadyScene.cpp 抽出）。
//
// 只放"数值与公式"，不放状态与绘制：改版式时改这一处，生命周期、模式分裂、
// 升级面板三个翻译单元共享同一份几何，不会出现"按钮挪了但热区没挪"。
// =============================================================================
namespace ready::layout {

// ===== UI 层叠 =====
// 主菜单控件组与护盾升级面板（模态）的层级：后注册者在同层内更靠上
constexpr int kLayerMenu = 20;
constexpr int kLayerUpgradePanel = 40;

// ===== 分裂维持区 =====
// 相对整行的外扩余量（与旧实现的行内判定等价）
constexpr float kRowZonePadX = 14.0f;
constexpr float kRowZonePadY = 16.0f;

// ===== 开始按钮与分裂式模式选择 =====
constexpr float kStartBtnY = 600.0f;      // 开始按钮行中心 y（InitButtons 布局共用）
constexpr float kModeSubBtnW = 112.0f;    // 分裂后单个模式按钮宽
constexpr float kModeSubBtnGap = 10.0f;   // 模式按钮间距
constexpr float kSplitHoverDelay = 0.2f;  // 开始按钮悬停多久后分裂
constexpr float kTipHoverDelay = 0.5f;    // 模式按钮悬停多久后弹出解释框

// 第 i 个模式按钮相对行中心的偏移，单位为「按钮宽 + 间距」。
// n=3 时等于 (i-1)，与原居中公式逐像素一致；n 变化时自动保持对称，
// 不再需要手写 (i-1) 或 (i-1.5f) 之类绑定数量的魔数。
inline float ModeOffsetX(int i, std::size_t modeCount) {
    return (float)i - ((float)modeCount - 1.0f) * 0.5f;
}

// 分裂行总宽：n 个按钮 + (n-1) 段间距；模式数为 0 时退化为 0（不参与命中）
inline float ModeRowWidth(std::size_t modeCount) {
    if (modeCount == 0) return 0.0f;
    return kModeSubBtnW * (float)modeCount + kModeSubBtnGap * (float)(modeCount - 1);
}

} // namespace ready::layout
