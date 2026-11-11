#pragma once
#include "scene/Scene.h"
#include "ui/UIWidget.h"   // 内联热区控件（分裂行/模式按钮/升级面板）需完整类型
#include "ui/GameInput.h"  // 作为 IGameInputSink 订阅推送式输入（ESC 关面板）
#include "ui/InputScope.h" // 主菜单组（非模态）+ 升级面板（模态）
#include "gameplay/ModeRegistry.h" // 模式热区数量由注册表动态给出
#include <array>
#include <vector>
#include <memory>

class Sprite;
class ButtonSprite;
class Services;

class ReadyScene : public Scene, public IGameInputSink {
public:
    explicit ReadyScene(Services& svc_);
    ~ReadyScene();

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(SpriteRenderer& renderer) override;
    void Reset() override;

    // IGameInputSink：接收 UISystem 每帧推送的已裁决输入快照（本场景只读 ESC）
    void OnGameInput(const GameInputFrame& frame) override { gameInput = frame; }

private:
    Services& svc_; // 组合根下发的服务聚合（构造期注入，生命周期长于本场景）

    // 本帧输入快照（由 OnGameInput 推送；Update 只读它，不再询设备）
    GameInputFrame gameInput;

    std::vector<std::shared_ptr<ButtonSprite>> menuButtons;
    std::shared_ptr<ButtonSprite> achievementEntry; // 右上角奖杯：直达成就视图（B7）

    std::vector<std::shared_ptr<Sprite>> terrainSprites;
    std::vector<std::shared_ptr<Sprite>> backgroundSprites;
    std::vector<std::shared_ptr<Sprite>> foregroundSprites;

    std::vector<std::shared_ptr<Sprite>> candleLights;

    std::shared_ptr<Sprite> batSprite;
    std::shared_ptr<Sprite> centerFrame;

    // LOGO 精灵
    std::shared_ptr<Sprite> logoSprite;

private:
    void InitScene();
    void InitButtons();
    void LoadSpritesFromResourceManager();
    void CreateSceneSprites();

    void ResetBatState();
    void ResetFixedSpritesState();

    void UpdateFixedSprites(float dt);
    void UpdateBat(float dt);
    void UpdateCandlelights(float dt);

    // ===== 开始按钮分裂式模式选择（B9 改版）：悬停触发，替代旧模式选择覆盖层 =====
    // 悬停"开始游戏"0.2s -> 按钮横向分裂成三个模式按钮（自中心滑出+展宽+淡入）；
    // 光标停留在整行横向区域内维持分裂，离开区域后平滑收缩回单按钮；
    // 悬停单个模式按钮 0.5s -> 光标旁淡入极小解释框；点击模式按钮（点击音效由 UISystem 集中播放）
    // 开始对应模式，点击后保持分裂状态直到转场切场景（返回主菜单时由 Reset 复位）。
    // 布局常量（开始按钮 y / 模式按钮宽与间距 / 悬停阈值）已集中到
    // ReadySceneLayout.h，三个翻译单元共用同一份几何。
    bool  splitOpen = false;          // 目标状态：true = 分裂展开
    float modeSplitProgress = 0.0f;   // 分裂进度（0 合并 ~ 1 完全分裂，双向动画）
    float rowHoverTimer = 0.0f;       // 开始按钮/横向区域悬停计时
    int   modeHoverIndex = -1;        // 当前悬停的模式按钮下标（-1 无）
    float modeHoverTimer = 0.0f;      // 该模式按钮连续悬停计时
    int   tipIndex = -1;              // 解释框当前展示的模式下标
    float tipFade = 0.0f;             // 解释框淡入淡出进度

    // ===== UI 交互控件（区域判定与事件派发均属 UISystem） =====
    int menuGroupId = -1;     // 主菜单：菜单按钮 + 奖杯入口 + 分裂行/模式按钮
    int upgradeGroupId = -1;  // 护盾升级面板（模态：弹开期间主菜单整体让位）
    InputScope menuScope;     // 主菜单组（非模态）
    InputScope upgradeScope;  // 升级面板（模态）
    // 热区控件以 shared_ptr 登记（所有权与本场景和 UISystem 共享）：
    // 标量在声明处创建，数组在 InitButtons 的现有循环里逐个创建后登记。
    std::shared_ptr<UIWidget> startRowZone = std::make_shared<UIWidget>();
                                        // 分裂后维持展开的整行区域（无自身绘制）
    std::vector<std::shared_ptr<UIWidget>> modeZones;
                                        // 模式按钮热区（InitButtons 中按注册表大小创建）
    std::shared_ptr<UIWidget> coinEntry = std::make_shared<UIWidget>();
                                        // 右上角金币入口（打开升级面板）
    std::shared_ptr<UIWidget> buyZones[2];
                                        // 两条升级轨道的购买热区（个数与 PlayerProfile::TrackCount 一致，InitButtons 中创建）
    std::shared_ptr<UIWidget> upgradeBackZone = std::make_shared<UIWidget>();
                                        // 升级面板“返回”热区

    void UpdateModeSplit(float dt);   // 悬停计时与分裂进度推进（悬停事实读自控件，不再自行判定）
    void DrawStartButtonRow();        // 分裂/收缩动效绘制（合并态由 ButtonSprite 绘制）
    void DrawModeTipBox();            // 光标旁的模式解释小框
    void DrawSplitButtonPlate(float cx, float cy, float w, float h, float alpha,
                              bool hovered, const char* text, float fontSize,
                              float textAlpha); // 带整体透明度的按钮底板（动效专用）
    void StartGameWithMode(int modeIndex); // 下标 -> ModeRegistry::At(i).Key()，不再是枚举强转

    // ===== 护盾升级覆盖层（B8）：金币入口按钮 + 升级面板 =====
    bool upgradePanelOpen = false;
    float insufficientMsgTimer = 0.0f; // 「金币不足」提示剩余显示时间
    void DrawUpgradeEntryButton(); // 右上角金币入口（奖杯图标左侧：金币图标 + 持有数 + 升级标签）
    void DrawUpgradePanelUI();     // 升级面板：持续时间/充能速度两条轨道
};
