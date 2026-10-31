#pragma once
#include "scene/Scene.h"
#include "ui/UIWidget.h"   // 音量滑条热区控件需完整类型
#include "ui/InputScope.h" // 设置页控件组（非模态）
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

    // ===== UI 交互控件（拖动归属由 UISystem 维护） =====
    int settingsGroupId = -1; // 设置页控件组：返回按钮 + 三条音量滑条
    InputScope settingsScope;
    std::shared_ptr<UIWidget> sliders[3];
                            // 全局/背景音乐/音效滑条（按下即捕获；构造函数中逐个创建后登记）

    void SyncFromAudioSystem();
    void DrawImGuiLayout();
    float& VolumeRef(int idx);           // 按下标取对应音量缓存字段
    void   ApplyVolume(int idx, float v); // 写入 AudioSystem 并实时持久化
};
