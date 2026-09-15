#pragma once
#include "Layer.h"
#include <memory>
#include <functional>

class Sprite;
class ButtonSprite;

class MainSceneUILayer : public Layer {
public:
    MainSceneUILayer();
    ~MainSceneUILayer() = default;

    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

    bool GetPauseState() const;

    // 当前鼠标是否悬停在任一当前显示的按钮上（几何与 ButtonSprite 命中判定一致）。
    // 供场景区分"点击按钮"与"点击游戏区域"，避免按钮点击同时触发游戏输入
    bool IsPointerOverButtons() const;

private:
    // 顶部图标按钮（暂停 / 返回 / 退出）
    std::shared_ptr<ButtonSprite> pauseButton;
    std::shared_ptr<ButtonSprite> homeButton;
    std::shared_ptr<ButtonSprite> quitButton;

    // 暂停面板按钮
    std::shared_ptr<ButtonSprite> resumeButton;
    std::shared_ptr<ButtonSprite> restartButton;
    std::shared_ptr<ButtonSprite> homeButtonInPanel;

    // GL 绘制的面板元素
    std::shared_ptr<Sprite> scrimSprite;       // 全屏遮罩
    std::shared_ptr<Sprite> panelSprite;       // 面板底图
    std::shared_ptr<Sprite> pausedTextSprite;  // PAUSED 标题图

    bool isPausing = false;

    // 外部注入：暂停面板的"重新开始"调用
    std::function<void()> onRestartCallback;

public:
    void SetRestartCallback(std::function<void()> cb) { onRestartCallback = std::move(cb); }

private:
    void InitAllButtons();
};
