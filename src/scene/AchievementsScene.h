#pragma once
#include "scene/Scene.h"
#include "ui/UIWidget.h"   // 滚轮区/滚动条等内联热区控件需完整类型
#include "ui/InputScope.h" // 成就页控件组（非模态）
#include <memory>

class Sprite;
class ButtonSprite;
class Services;

// 成就场景：主界面奖杯入口直达；原为 ScoreRankScene 内嵌成就视图，现拆分为独立场景
class AchievementsScene : public Scene {
public:
    explicit AchievementsScene(Services& svc_);
    ~AchievementsScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

private:
    Services& svc_; // 构造期注入的服务聚合

    std::shared_ptr<ButtonSprite> backToReady;
    std::shared_ptr<Sprite> background;
    std::shared_ptr<Sprite> titleSprite;

    // ===== 成就列表滚动状态（与榜单同款交互：滚轮/滑块拖拽/轨道翻页） =====
    float achScroll = 0.0f;
    float achScrollTarget = 0.0f;
    float achScrollbarGrabOffset = 0.0f;
    // 是否正在拖拽不再自持标志：直读 scrollThumb->IsCaptured()

    // ===== UI 交互控件（区域判定与事件派发均属 UISystem） =====
    int achGroupId = -1;    // 成就页控件组：返回按钮 + 滚轮区 + 滑轨 + 滑块
    InputScope achScope;
    // 热区控件以 shared_ptr 登记（所有权与本场景和 UISystem 共享），在声明处创建
    std::shared_ptr<UIWidget> listZone = std::make_shared<UIWidget>();
                            // 列表面板：悬停于此接受滚轮滚动
    std::shared_ptr<UIWidget> scrollTrack = std::make_shared<UIWidget>();
                            // 滑轨空白区：点击翻页
    std::shared_ptr<UIWidget> scrollThumb = std::make_shared<UIWidget>();
                            // 滑块：按下即捕获，拖拽直接定位

    void DrawAchievements();
};
