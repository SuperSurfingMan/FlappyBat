#pragma once
#include "Scene.h"
#include "../ui/UIWidget.h"   // 滚轮区/滚动条等内联热区控件需完整类型
#include <memory>

class Sprite;
class ButtonSprite;

// 成就场景：主界面奖杯入口直达；原为 ScoreRankScene 内嵌成就视图，现拆分为独立场景
class AchievementsScene : public Scene {
public:
    AchievementsScene();
    ~AchievementsScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

private:
    std::shared_ptr<ButtonSprite> backToReady;
    std::shared_ptr<Sprite> background;
    std::shared_ptr<Sprite> titleSprite;

    // ===== 成就列表滚动状态（与榜单同款交互：滚轮/滑块拖拽/轨道翻页） =====
    float achScroll = 0.0f;
    float achScrollTarget = 0.0f;
    float achScrollbarGrabOffset = 0.0f;
    // 是否正在拖拽不再自持标志：直读 scrollThumb.IsCaptured()

    // ===== UI 交互控件（区域判定与事件派发均属 UISystem） =====
    int achGroupId = -1;    // 成就页控件组：返回按钮 + 滚轮区 + 滑轨 + 滑块
    UIWidget listZone;      // 列表面板：悬停于此接受滚轮滚动
    UIWidget scrollTrack;   // 滑轨空白区：点击翻页
    UIWidget scrollThumb;   // 滑块：按下即捕获，拖拽直接定位

    void DrawAchievements();
};
