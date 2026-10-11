#pragma once
#include "Scene.h"
#include "../app/ScoreRank.h"
#include "../ui/UIWidget.h"   // 页签/滚动条/遮罩等内联热区控件需完整类型
#include <string>
#include <vector>
#include <memory>

class Sprite;
class ButtonSprite;
class Layer;

class ScoreRankScene : public Scene {
public:
    ScoreRankScene();
    ~ScoreRankScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

private:
    std::shared_ptr<ButtonSprite> backToReady;
    std::shared_ptr<ButtonSprite> syncAndLoad;
    std::shared_ptr<ButtonSprite> clearRecords;   // 清空记录（危险操作入口）
    std::shared_ptr<ButtonSprite> confirmYes;     // 确认弹窗：确认清空
    std::shared_ptr<ButtonSprite> confirmNo;      // 确认弹窗：取消
    std::shared_ptr<Sprite> background;
    std::shared_ptr<Sprite> titleSprite;

    std::vector<size_t> scoreRank;
    std::shared_ptr<Layer> toastLayer;

    // ===== 模式切换（三模式独立榜单）：当前查看的模式页签 =====
    ScoreRank::RankMode viewMode = ScoreRank::RankMode::Classic;

    // ===== 清空记录二次确认弹窗 =====
    bool  confirmClearOpen = false; // 弹窗是否打开
    float confirmFade = 0.0f;       // 弹窗淡入进度（0~1）

    // ===== 榜单滚动状态（视口内平滑滚动 + 右侧滑块拖拽） =====
    float listScroll = 0.0f;        // 当前列表滚动量（像素，向渲染值平滑趋近）
    float listScrollTarget = 0.0f;  // 目标滚动量（滚轮/滑块/点击滑轨设置）
    float scrollbarGrabOffset = 0.0f; // 按下时鼠标相对滑块顶部的偏移
    // 是否正在拖拽不再自持标志：直读 scrollThumb->IsCaptured()（捕获归属由 UISystem 维护）

    // ===== UI 交互控件（区域判定与事件派发均属 UISystem） =====
    int rankGroupId = -1;   // 榜单主控件组：按钮 + 页签 + 滚轮区 + 滑轨 + 滑块
    int dialogGroupId = -1; // 清空确认弹窗（模态）
    // 热区控件以 shared_ptr 登记（所有权与本场景和 UISystem 共享）：
    // 标量在声明处创建，tabs 在构造函数现有循环里逐个创建后登记。
    std::shared_ptr<UIWidget> tabs[3];
                            // 模式页签（个数与 ScoreRank::ModeCount 一致，构造函数中创建）
    std::shared_ptr<UIWidget> listZone = std::make_shared<UIWidget>();
                            // 榜单视口：悬停于此接受滚轮滚动
    std::shared_ptr<UIWidget> scrollTrack = std::make_shared<UIWidget>();
                            // 滑轨空白区：点击翻页
    std::shared_ptr<UIWidget> scrollThumb = std::make_shared<UIWidget>();
                            // 滑块：按下即捕获，拖拽直接定位
    std::shared_ptr<UIWidget> scrimZone = std::make_shared<UIWidget>();
                            // 弹窗全屏遮罩：点击遮罩 = 取消
    std::shared_ptr<UIWidget> dialogBody = std::make_shared<UIWidget>();
                            // 弹窗面板本体：吞掉面板内空白处的点击（不误判为“点遮罩取消”）

    void LoadRankList();
    void SwitchViewMode(ScoreRank::RankMode mode); // 切换页签并重载对应模式榜单
    void DrawModeTabs(float panelX, float panelTop); // 面板顶部的模式页签行
    void DrawImGUIText();
    void DrawImGuiRank();

    // ===== 清空记录确认弹窗 =====
    void OpenClearConfirm();      // 打开弹窗（挂起其他交互）
    void CloseClearConfirm();     // 关闭弹窗（恢复交互）
    void DrawClearConfirmDialog();// 绘制遮罩 + 弹窗面板与文案
    void ConfirmDialogRect(float& cx, float& cy, float& w, float& h) const;
};
