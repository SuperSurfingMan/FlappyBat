#pragma once
#include "Layer.h"
#include "../ui/GameInput.h"
#include <memory>
#include <functional>

class Sprite;
class ButtonSprite;

// 暂停系统 UI 图层：作为 IGameInputSink 订阅推送式输入，只读暂停快捷键（ESC/P/R），
// 不再自行逐帧询问 Input 设备（设备采集集中于 UISystem）。
class MainSceneUILayer : public Layer, public IGameInputSink {
public:
    MainSceneUILayer();
    ~MainSceneUILayer() override;

    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

    // IGameInputSink：接收 UISystem 每帧推送的已裁决输入快照
    void OnGameInput(const GameInputFrame& frame) override { gameInput = frame; }

    bool GetPauseState() const;

    // 局内 UI 是否参与交互：入场倒数 / 转场 / 结算期间由 MainScene 置 false。
    // 本图层的组开关与模态压栈都在此处集中同步（命中判定归 UISystem）。
    void SetInteractive(bool interactive);

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
    bool interactive = true;

    // 本帧输入快照（仅供 Update 读暂停快捷键；由 OnGameInput 推送）
    GameInputFrame gameInput;

    // UI 组 id（由 UISystem 分配）：图标排非模态，暂停面板模态压栈
    int hudGroupId = -1;
    int pauseGroupId = -1;

    // 外部注入：暂停面板的"重新开始"调用
    std::function<void()> onRestartCallback;

public:
    void SetRestartCallback(std::function<void()> cb) { onRestartCallback = std::move(cb); }

private:
    void InitAllButtons();
    void SyncInputGroups();
};
