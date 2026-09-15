#pragma once
#include "Scene.h"
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
    bool  achScrollbarDragging = false;
    float achScrollbarGrabOffset = 0.0f;

    void DrawAchievements();
};
