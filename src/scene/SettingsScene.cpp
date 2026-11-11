#include "app/Services.h"
#include "scene/SettingsScene.h"
#include "enemy/owlking/OwlKing.h"
#include "sprite/ButtonSprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"
#include "res/ResourceManager.h"
#include "sprite/Sprite.h"
#include "core/Config.h"
#include "audio/AudioSystem.h"
#include "scene/SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

SettingsScene::SettingsScene(Services& services) : svc_(services) {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();

    // 背景：复用排行榜场景的钟楼内景
    auto bgTex = svc_.Res().GetTexture2D("v2_ui_scoreboard_bg");
    if (!bgTex) bgTex = svc_.Res().GetTexture2D("scoreRankBackground");
    if (bgTex) {
        background = std::make_shared<Sprite>(bgTex);
        background->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
        background->SetScale(WIN_WIDTH, WIN_HEIGHT);
    }

    // 底部主按钮：返回主菜单
    backToReady = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f, WIN_HEIGHT - 90.f},
        Vec2{UITheme::MainBtnW, UITheme::MainBtnH},
        "返回主菜单");
    backToReady->SetOnClick([this]() {
        svc_.Scenes().RequestScene(kSceneReady);
    });

    // UI 控件组：返回按钮与三条滑条均登记到 UISystem；滑条按下即捕获（拖出轨道仍跟随）
    UISystem& ui = svc_.UI();
    settingsGroupId = ui.CreateGroup(20); // 与其它场景主控件同层
    settingsScope.Bind(ui, settingsGroupId, false);
    ui.Add(settingsGroupId, backToReady);
    for (int i = 0; i < 3; ++i) {
        sliders[i] = std::make_shared<UIWidget>();
        sliders[i]->SetCaptureOnPress(true);
        sliders[i]->SetPlayFeedbackSFX(false); // 拖动过程不发声（与旧实现一致）
        ui.Add(settingsGroupId, sliders[i]);
    }
    // 枭王形象分段切换（legacy 归一化帧 / Vector 矢量帧）：点击循环切换并持久化
    artToggle = std::make_shared<UIWidget>();
    artToggle->SetOnClick([]() {
        const bool toVector = OwlKing::GetArtBatch() != OwlKing::ArtBatch::Vector;
        OwlKing::SetArtBatch(toVector ? OwlKing::ArtBatch::Vector
                                      : OwlKing::ArtBatch::Legacy);
    });
    ui.Add(settingsGroupId, artToggle);
    // 栏目页签（最后注册：盖在面板内容热区之上，同排行榜页签注册惯例）
    for (int i = 0; i < 2; ++i) {
        tabs[i] = std::make_shared<UIWidget>();
        tabs[i]->SetOnClick([this, i]() { SwitchTab((i == 0) ? Tab::Volume : Tab::OwlArt); });
        ui.Add(settingsGroupId, tabs[i]);
    }
    tabs[0]->SetEnabled(false); // 选中页签不可点，后续由 DrawModeTabs 逐帧刷新
}

void SettingsScene::SwitchTab(Tab t) {
    if (t == tab) return;
    tab = t;
}

float& SettingsScene::VolumeRef(int idx) {
    switch (idx) {
        case 1:  return bgmVolume;
        case 2:  return sfxVolume;
        default: return masterVolume;
    }
}

void SettingsScene::ApplyVolume(int idx, float v) {
    switch (idx) {
        case 0: svc_.Audio().SetMasterVolume(v); break;
        case 1: svc_.Audio().SetBGMVolume(v);    break;
        case 2: svc_.Audio().SetSFXVolume(v);    break;
    }
    svc_.Audio().SaveSettings(); // 滑块改变即实时应用并持久化
}

SettingsScene::~SettingsScene() {}

void SettingsScene::OnEnter() {
    // 进入时从音频系统读取当前音量到本地缓存（供滑块显示）
    SyncFromAudioSystem();
    settingsScope.SetEnabled(true);
}

void SettingsScene::OnExit() {
    // 离开本页：关闭控件组（拖动中的捕获会由 UISystem 自动 Cancel）
    settingsScope.SetEnabled(false);
}

void SettingsScene::Update(float dt) {
    backToReady->Update(dt);
}

void SettingsScene::Render(SpriteRenderer& renderer) {
    if (background) renderer.Draw_UV(background);
    DrawImGuiLayout();
    backToReady->DrawLayout();
}

void SettingsScene::Reset() {
    if (backToReady) backToReady->Reset();
    SyncFromAudioSystem();
    tab = Tab::Volume; // 回到音量栏目
    svc_.UI().ClearWidgetState(settingsGroupId);
}

void SettingsScene::SyncFromAudioSystem() {
    masterVolume = svc_.Audio().GetMasterVolume();
    bgmVolume    = svc_.Audio().GetBGMVolume();
    sfxVolume    = svc_.Audio().GetSFXVolume();
}

// ===== 音量面板几何常量（风格与排行榜面板统一：暗色圆角面板 + 金色胶囊滑轨 + 金币圆钮） =====
namespace {
constexpr float kVolLabelSize    = 28.f;  // 行标签字号（全局音量/背景音乐/音效）
constexpr float kVolPctSize      = 22.f;  // 百分比字号
constexpr float kVolPanelW       = 460.f; // 面板宽（与排行榜面板同宽）
constexpr float kVolPadX         = 38.f;  // 面板左右内边距
constexpr float kVolPadTop       = 34.f;  // 面板顶边距首行标签
constexpr float kVolPadBottom    = 30.f;  // 末行滑块底缘距面板底边
constexpr float kVolTrackH       = 16.f;  // 滑轨胶囊高度
constexpr float kVolThumbR       = 15.f;  // 滑块圆钮半径（直径 30，明显大于轨道便于抓取）
constexpr float kVolLabelToTrack = 46.f;  // 标签中心 -> 滑轨中心
constexpr float kVolRowGap       = 48.f;  // 滑轨中心 -> 下一行标签中心
// ===== 栏目页签行几何（与排行榜 DrawModeTabs 同款） =====
constexpr float kTabW      = 138.f; // 单个页签宽
constexpr float kTabH      = 48.f;  // 单个页签高
constexpr float kTabGap    = 12.f;  // 页签间距
constexpr float kTabTopPad = 26.f;  // 面板顶边到页签行的留白
constexpr float kTabContentGap = 14.f; // 页签行下缘到栏目内容区顶的间隔
}

void SettingsScene::DrawImGuiLayout() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();

    // ===== 面板几何：页签行 + 当前栏目内容，高度按音量栏目（较高者）精确计算 =====
    // 栏目内容布局保持分栏前不变：音量栏目行布局 标签 -> (kVolLabelToTrack) -> 滑轨 -> (kVolRowGap) -> 下一行标签
    const float labelH     = kVolLabelSize + 4.f;
    const float rowPitch   = kVolLabelToTrack + kVolRowGap;
    const float contentTop = kTabTopPad + kTabH + kTabContentGap; // 内容区顶（相对 panelTop，页签行下方）
    const float panelH     = contentTop + kVolPadTop + labelH * 0.5f + 3.f * kVolLabelToTrack
                           + 2.f * kVolRowGap + kVolThumbR + kVolPadBottom;
    const float panelX      = WIN_WIDTH * 0.5f;
    const float panelY      = WIN_HEIGHT * 0.5f;
    const float panelTop    = panelY - panelH * 0.5f;
    const float panelBottom = panelY + panelH * 0.5f;
    const float panelLeft   = panelX - kVolPanelW * 0.5f;
    const float panelRight  = panelX + kVolPanelW * 0.5f;
    const float trackLeft   = panelLeft + kVolPadX;
    const float trackRight  = panelRight - kVolPadX;
    const float trackW      = trackRight - trackLeft;
    const float row1LabelY  = panelTop + contentTop + kVolPadTop + labelH * 0.5f;
    const float row1TrackY  = row1LabelY + kVolLabelToTrack;

    // ===== 标题（面板上方，同排行榜 标题-页签面板-主按钮 的纵向次序） =====
    UIBuilder::DrawCenteredText("声音设置", panelX, panelTop - 34.f,
                                UITheme::FontH2, UITheme::ToImU32(UITheme::Amber));

    // 面板底板：暗色圆角 + 暗金描边（与排行榜面板/页签同款）
    UIBuilder::DrawRoundedRect(panelX, panelY, kVolPanelW, panelH, 16.f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // ===== 栏目页签行（面板顶部）：模仿排行榜，点击切换子栏目 =====
    DrawModeTabs(panelX, panelTop);

    // 带阴影文字（左对齐 / 右对齐）
    auto drawText = [&](const std::string& text, float x, float cy, float size,
                        ImU32 color, bool rightAlign) {
        if (!font) return;
        ImVec2 ts = UIBuilder::CalcTextSize(text, size);
        ImVec2 pos(rightAlign ? x - ts.x : x, cy - ts.y * 0.5f);
        dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), text.c_str());
        dl->AddText(font, size, pos, color, text.c_str());
    };

    if (tab == Tab::Volume) {
        // ===== 音量栏目：三行音量数据（UI 与分栏前一致） =====
        static const char* labels[3] = { "全局音量", "背景音乐", "音效" };
        float* values[3] = { &masterVolume, &bgmVolume, &sfxVolume };
        for (int i = 0; i < 3; ++i) sliders[i]->SetEnabled(true);
        artToggle->SetEnabled(false); // 隐藏栏目控件一并禁用（热区失效，避免误触）

        // 拖拽交互登记：热区与滑轨几何同源，判定与派发归 UISystem
        // 按下即定位（按下当帧也会收到一次 Drag），按住后跟随光标，松开结束
        for (int i = 0; i < 3; ++i) {
            const float trackY = row1TrackY + (float)i * rowPitch;
            sliders[i]->SetRect(UIRect::FromBounds(trackLeft - kVolThumbR, trackY - kVolThumbR - 8.f,
                                                  trackRight + kVolThumbR, trackY + kVolThumbR + 8.f));
            sliders[i]->SetOnDrag([this, i, trackLeft, trackW](const UIEvent& e) {
                const float t = std::clamp((e.pointer.x - trackLeft) / trackW, 0.f, 1.f);
                float& v = VolumeRef(i);
                if (t != v) {
                    v = t;
                    ApplyVolume(i, t);
                }
            });
        }

        // ===== 逐行绘制：分隔线 -> 标签/百分比 -> 胶囊滑轨 -> 金币圆钮 =====
        for (int i = 0; i < 3; ++i) {
            const float labelY = row1LabelY + (float)i * rowPitch;
            const float trackY = row1TrackY + (float)i * rowPitch;
            const float value  = *values[i];
            const float thumbX = trackLeft + value * trackW;
            const bool active  = sliders[i]->IsCaptured();
            // 悬停/拖拽中高亮整行滑轨（命中区为整条滑轨，滑块与轨道一致亮起）
            const bool trackHot = active || sliders[i]->IsHovered();
            const bool thumbHot = trackHot;

            // 行间分隔线（暗金，与榜单行分隔同款）
            if (i > 0) {
                const float gapTop    = trackY - rowPitch + kVolThumbR; // 上一行滑块底缘
                const float gapBottom = labelY - labelH * 0.5f;         // 本行标签顶缘
                const float divY = (gapTop + gapBottom) * 0.5f;
                dl->AddLine(ImVec2(panelLeft + 50.f, divY), ImVec2(panelRight - 50.f, divY),
                            UITheme::ToImU32(UITheme::AmberDim, 0.5f));
            }

            // 标签（拖拽中的行高亮为金色）+ 右对齐百分比
            drawText(labels[i], panelLeft + kVolPadX, labelY, kVolLabelSize,
                     active ? UITheme::ToImU32(UITheme::Amber)
                            : UITheme::ToImU32(UITheme::Moonlight), false);
            char pct[16];
            snprintf(pct, sizeof(pct), "%.0f%%", value * 100.f);
            drawText(pct, panelRight - kVolPadX, labelY, kVolPctSize,
                     UITheme::ToImU32(UITheme::Amber), true);

            // 滑轨：暗槽胶囊（悬停提亮）+ 金色已填充胶囊
            dl->AddRectFilled(ImVec2(trackLeft, trackY - kVolTrackH * 0.5f),
                              ImVec2(trackRight, trackY + kVolTrackH * 0.5f),
                              UITheme::ToImU32({0.12f, 0.10f, 0.18f}, trackHot ? 1.0f : 0.9f),
                              kVolTrackH * 0.5f);
            if (thumbX - trackLeft > 1.f) {
                // 填充段圆角随宽度收缩，避免极小值时胶囊变形
                const float fillR = std::min(kVolTrackH * 0.5f, (thumbX - trackLeft) * 0.5f);
                dl->AddRectFilled(ImVec2(trackLeft, trackY - kVolTrackH * 0.5f),
                                  ImVec2(thumbX, trackY + kVolTrackH * 0.5f),
                                  UITheme::ToImU32(UITheme::Amber, 0.9f), fillR);
            }

            // 滑块圆钮：阴影 + 金色主体 + 内环高光（与金币 HUD 同款质感）
            const ImU32 thumbCol = active   ? UITheme::ToImU32({1.0f, 0.87f, 0.52f})
                                 : thumbHot ? UITheme::ToImU32(UITheme::Amber)
                                            : UITheme::ToImU32(UITheme::Amber, 0.95f);
            dl->AddCircleFilled(ImVec2(thumbX + 1.5f, trackY + 2.f), kVolThumbR,
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)), 20);
            dl->AddCircleFilled(ImVec2(thumbX, trackY), kVolThumbR, thumbCol, 20);
            dl->AddCircle(ImVec2(thumbX, trackY), kVolThumbR - 4.5f,
                          UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 16, 2.0f);
        }
    } else {
        // ===== 枭王形象栏目：分段切换控件原样保留，移入面板内容区垂直居中 =====
        for (int i = 0; i < 3; ++i) sliders[i]->SetEnabled(false); // 隐藏栏目控件禁用
        artToggle->SetEnabled(true);

        // 两段胶囊（经典 | 矢量）：选中段金色填充、未选中暗槽；点击任意处循环切换并持久化
        const float segW      = kVolPanelW - 2.f * kVolPadX;
        const float segH      = 44.f;
        const float contentCy = (panelTop + contentTop + panelBottom - kVolPadBottom) * 0.5f;
        const float segY      = contentCy + 16.f; // 胶囊中心（标签在胶囊上方 22px，同原相对结构）
        const float segLeft   = panelLeft + kVolPadX;
        const float segRight  = segLeft + segW;
        const float segMid    = segLeft + segW * 0.5f;
        artToggle->SetRect(UIRect::FromBounds(segLeft, segY - segH * 0.5f,
                                              segRight, segY + segH * 0.5f));
        const OwlKing::ArtBatch batch = OwlKing::GetArtBatch();
        const bool vectorSel = (batch == OwlKing::ArtBatch::Vector);
        const bool segHot = artToggle->IsHovered() || artToggle->IsCaptured();

        // 小节标签（左对齐，同音量行标签字号）
        drawText("枭王形象", panelLeft + kVolPadX, segY - segH * 0.5f - 22.f, kVolPctSize,
                 UITheme::ToImU32(UITheme::Moonlight), false);

        // 两段胶囊：整条暗槽底板 + 选中段金色浮层 + 分段文字
        dl->AddRectFilled(ImVec2(segLeft, segY - segH * 0.5f), ImVec2(segRight, segY + segH * 0.5f),
                          UITheme::ToImU32({0.12f, 0.10f, 0.18f}, segHot ? 1.0f : 0.9f), segH * 0.5f);
        {
            const float x0 = vectorSel ? segMid : segLeft;
            const float x1 = vectorSel ? segRight : segMid;
            const float fillR = std::min(segH * 0.5f, (x1 - x0) * 0.5f);
            dl->AddRectFilled(ImVec2(x0, segY - segH * 0.5f), ImVec2(x1, segY + segH * 0.5f),
                              UITheme::ToImU32(UITheme::Amber, 0.92f), fillR);
        }
        auto segText = [&](const char* s, float cx, bool sel) {
            const ImU32 col = sel ? UITheme::ToImU32({0.16f, 0.10f, 0.05f})
                                  : UITheme::ToImU32(UITheme::AmberDim, 0.9f);
            UIBuilder::DrawCenteredText(s, cx, segY, 20.f, col);
        };
        segText("经典", segLeft + segW * 0.25f, !vectorSel);
        segText("矢量", segMid + segW * 0.25f, vectorSel);
    }
}

// ===== 栏目页签行：音量设置 / 枭王形象（当前栏目高亮；样式与排行榜 DrawModeTabs 同款） =====
void SettingsScene::DrawModeTabs(float panelX, float panelTop) {
    const float rowY = panelTop + kTabTopPad + kTabH * 0.5f;
    static const char* kTabNames[2] = { "音量设置", "枭王形象" };

    for (int i = 0; i < 2; ++i) {
        // 两个页签以 panelX 为中心对称排布
        const float tx = panelX + ((float)i - 0.5f) * (kTabW + kTabGap);
        const bool selected = ((i == 0) == (tab == Tab::Volume));

        // 热区与页签底板同源；选中项不可点（避免无效高亮与音效，同排行榜惯例）
        tabs[i]->SetRect(UIRect::FromCenter(tx, rowY, kTabW, kTabH));
        tabs[i]->SetEnabled(!selected);
        const bool hovered = tabs[i]->IsHovered();

        // 底板：选中 = 主按钮紫 + 亮金描边；未选中 = 面板同款暗色 + 暗金描边（悬停提亮）
        UIBuilder::DrawRoundedRect(tx, rowY, kTabW, kTabH, 12.f,
            selected ? UITheme::ToImU32({0.243f, 0.169f, 0.396f}, 0.95f)
                     : UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
            selected ? UITheme::ToImU32(UITheme::Amber)
                     : UITheme::ToImU32(UITheme::AmberDim, hovered ? 0.95f : 0.55f),
            selected ? 3.0f : 2.0f);

        // 文字：选中金色，未选中月光白（悬停提亮）
        UIBuilder::DrawCenteredText(kTabNames[i], tx, rowY, 22.f,
            selected ? UITheme::ToImU32(UITheme::Amber)
                     : UITheme::ToImU32(UITheme::Moonlight, hovered ? 0.95f : 0.75f));
    }

    // 页签行与栏目内容之间的分隔线（宽度与页签行对齐）
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float halfRowW = (2.f * kTabW + kTabGap) * 0.5f;
    const float lineY = rowY + kTabH * 0.5f + 8.f;
    dl->AddLine(ImVec2(panelX - halfRowW, lineY), ImVec2(panelX + halfRowW, lineY),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f));
}
