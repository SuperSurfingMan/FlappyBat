#pragma once
#include "Scene.h"
#include "../app/ScoreRank.h"
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
    bool  scrollbarDragging = false; // 正在拖拽滑块
    float scrollbarGrabOffset = 0.0f; // 按下时鼠标相对滑块顶部的偏移

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
