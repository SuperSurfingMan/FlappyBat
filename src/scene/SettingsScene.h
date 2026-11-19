#pragma once
#include "scene/Scene.h"
#include "ui/UIWidget.h"   // 音量滑条热区控件需完整类型
#include "ui/InputScope.h" // 设置页控件组（非模态）
#include "app/TuningParams.h" // 参数调节栏目的行数/行规格来源
#include <memory>

class Sprite;
class ButtonSprite;
class Services;

// 设置场景：用 ImGui 滑块让玩家实时调整参数并持久化
// 分栏（模仿排行榜页签）：「音量设置」「参数调节」「枭王形象」「自动演示」四个子栏目，
// 面板顶部的页签行切换，各栏目内部 UI 与切换前保持一致。
// 「参数调节」的行内容（标签 / 范围 / 量化 / 读数格式）全部来自 app/TuningParams.h
// 的参数表——本类只把鼠标位置归一化成 0..1 并交给参数对象，不复制数值规则；
// 参数表加一行，本栏目自动多一行滑条。
class SettingsScene : public Scene {
public:
    explicit SettingsScene(Services& svc_);
    ~SettingsScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

private:
    Services& svc_; // 构造期注入的服务聚合

    // ===== 子栏目（页签切换；选中页签禁用自身，同排行榜惯例）=====
    // 序号即页签排列序（构造期的页签登记与 DrawModeTabs 的排布都按它来），
    // 新增栏目：枚举加值 + 名字表加一项 + 内容分支加一段
    enum class Tab { Volume = 0, Params, OwlArt, AutoPlay, Count };
    static constexpr int TabCount = (int)Tab::Count;
    void SwitchTab(Tab t);

    std::shared_ptr<Sprite> background;
    std::shared_ptr<ButtonSprite> backToReady;

    Tab tab = Tab::Volume; // 当前栏目（每次进入/Reset 回音量栏目）

    // 本地缓存的音量值（OnEnter 时从 AudioSystem 读入，滑块绑定此处）
    float masterVolume = 1.0f;
    float bgmVolume    = 1.0f;
    float sfxVolume    = 1.0f;

    // ===== UI 交互控件（拖动归属由 UISystem 维护） =====
    int settingsGroupId = -1; // 设置页控件组：返回按钮 + 页签 + 各栏目滑条/切换
    InputScope settingsScope;
    std::shared_ptr<UIWidget> tabs[TabCount]; // 栏目页签（选中项禁用）
    std::shared_ptr<UIWidget> sliders[3];
                            // 全局/背景音乐/音效滑条（按下即捕获；构造函数中逐个创建后登记）
    std::shared_ptr<UIWidget> paramSliders[TuningParams::Count];
                            // 参数调节滑条（直连 TuningParams，拖动即生效并持久化）
    std::shared_ptr<UIWidget> artToggle; // 枭王形象分段切换（legacy/Vector，点击循环切换）
    std::shared_ptr<UIWidget> autoPlayToggle; // 自动演示开关（开启/关闭，直连 AutoPilotDirector 并持久化）

    void SyncFromAudioSystem();
    void DrawImGuiLayout();
    void DrawModeTabs(float panelX, float panelTop); // 页签行（模仿排行榜 DrawModeTabs）
    float& VolumeRef(int idx);           // 按下标取对应音量缓存字段
    void   ApplyVolume(int idx, float v); // 写入 AudioSystem 并实时持久化
};
