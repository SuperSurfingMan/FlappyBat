#pragma once
#include "layer/Layer.h"
#include "core/Timer.h"
#include "ui/InputScope.h"
#include <functional>
#include <memory>

class Sprite;
class ButtonSprite;
class Services;

class MainSceneOverLayer : public Layer {
public:
    explicit MainSceneOverLayer(Services& svc_);
    ~MainSceneOverLayer() = default;

    void Reset() override;

    void SetRestartEvent(std::function<void()> event) { onRestart = std::move(event); }
    void ActivateLayer();

    void SetCurrentScore(int s) { currentScore = s; }
    void SetBestScore(int s) { bestScore = s; }

protected:
    void OnUpdate(float dt) override;
    void OnDraw(SpriteRenderer& renderer) override;

    void OnAttach() override;
    void OnDetach() override;
    void OnInputEnabledChanged(bool enabled) override;

private:
    Services& svc_; // 构造期注入的服务聚合

    std::shared_ptr<Sprite> gameOverImage;
    std::shared_ptr<ButtonSprite> restartButton;
    std::shared_ptr<ButtonSprite> backToReadyButton;

    // GL 绘制的面板元素
    std::shared_ptr<Sprite> scrimSprite;          // 全屏遮罩
    std::shared_ptr<Sprite> panelSprite;          // 面板底图
    std::shared_ptr<Sprite> gameOverTextSprite;   // GAME OVER 标题图

    // UI 组 id（模态）：面板激活期间仅本组控件可命中
    int overGroupId = -1;
    InputScope overScope;

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
