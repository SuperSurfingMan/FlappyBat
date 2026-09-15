#pragma once
#include "Scene.h"
#include <memory>

class Sprite;
class ButtonSprite;

// 声音设置场景：用 ImGui 滑块让玩家实时调整全局/BGM/音效音量，并持久化
class SettingsScene : public Scene {
public:
    SettingsScene();
    ~SettingsScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

private:
    std::shared_ptr<Sprite> background;
    std::shared_ptr<ButtonSprite> backToReady;

    // 本地缓存的音量值（OnEnter 时从 AudioSystem 读入，滑块绑定此处）
    float masterVolume = 1.0f;
    float bgmVolume    = 1.0f;
    float sfxVolume    = 1.0f;

    // 正在拖拽的音量滑块下标（-1 = 未拖拽；0/1/2 = 全局/背景音乐/音效）
    int draggingSlider = -1;

    void SyncFromAudioSystem();
    void DrawImGuiLayout();
};
