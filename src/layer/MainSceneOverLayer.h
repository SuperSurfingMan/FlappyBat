#pragma once
#include "Layer.h"
#include "../timer/Timer.h"
#include <functional>
#include <memory>

class Sprite;
class ButtonSprite;

class MainSceneOverLayer : public Layer {
public:
    MainSceneOverLayer();
    ~MainSceneOverLayer() = default;

    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

    void SetRestartEvent(std::function<void()> event) { onRestart = std::move(event); }
    void ActivateLayer();

    // 结算面板是否参与交互：离开主场景时由 MainScene 置 false（面板仍可能处于激活展示态）
    void SetInteractive(bool interactive);

    void SetCurrentScore(int s) { currentScore = s; }
    void SetBestScore(int s) { bestScore = s; }

private:
    std::shared_ptr<Sprite> gameOverImage;
    std::shared_ptr<ButtonSprite> restartButton;
    std::shared_ptr<ButtonSprite> backToReadyButton;

    // GL 绘制的面板元素
    std::shared_ptr<Sprite> scrimSprite;          // 全屏遮罩
    std::shared_ptr<Sprite> panelSprite;          // 面板底图
    std::shared_ptr<Sprite> gameOverTextSprite;   // GAME OVER 标题图

    bool layerActive = false;
    bool interactive = true;

    // UI 组 id（模态）：面板弹出期间仅本组控件可命中
    int overGroupId = -1;

    float blinkInterval = 2.0f;
    Timer blinkTimer;
    bool shouldBlink = true;

    int currentScore = 0;
    int bestScore = 0;

    std::function<void()> onRestart;

private:
    void InitSprites();
    void ResetAnimState();
    void DrawImGUILayout();
    void SyncInputGroups();
};
