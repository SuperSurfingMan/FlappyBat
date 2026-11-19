#include "app/Services.h"
#include "scene/SettingsScene.h"
#include "autoplay/AutoPilotDirector.h"
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
#include <functional>
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

    // UI 控件组：返回按钮与各栏目滑条均登记到 UISystem；滑条按下即捕获（拖出轨道仍跟随）
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
    // 自动演示开关（开启/关闭分段）：点击切换 AutoPilotDirector 并即时落盘（U 行）
    autoPlayToggle = std::make_shared<UIWidget>();
    autoPlayToggle->SetOnClick([this]() {
        svc_.AutoPilot().SetEnabled(!svc_.AutoPilot().Enabled());
    });
    ui.Add(settingsGroupId, autoPlayToggle);
    // 参数调节滑条：数量与行序都由 TuningParams 的参数表决定
    // （本文件不复制行的范围/步进/标签，参数表加一行此处自动多一条）
    for (int i = 0; i < TuningParams::Count; ++i) {
        paramSliders[i] = std::make_shared<UIWidget>();
        paramSliders[i]->SetCaptureOnPress(true);
        paramSliders[i]->SetPlayFeedbackSFX(false); // 连续拖动静音，避免刷音效
        ui.Add(settingsGroupId, paramSliders[i]);
    }
    // 栏目页签（最后注册：盖在面板内容热区之上，同排行榜页签注册惯例）
    for (int i = 0; i < TabCount; ++i) {
        tabs[i] = std::make_shared<UIWidget>();
        tabs[i]->SetOnClick([this, i]() { SwitchTab((Tab)i); }); // 页签序号即 Tab 枚举值
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
    // 进入时从音频系统读取当前音量到本地缓存（供滑块显示）；
    // 参数调节栏目的数值直接读 TuningParams（服务在组合根已载入存档），无需再同步
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

// ===== 面板几何常量（风格与排行榜面板统一：暗色圆角面板 + 金色胶囊滑轨 + 金币圆钮） =====
namespace {
constexpr float kVolLabelSize    = 28.f;  // 行标签字号（全局音量/背景音乐/音效）
constexpr float kVolPctSize      = 22.f;  // 百分比字号（参数栏目读数同字号）
constexpr float kVolPanelW       = 460.f; // 面板宽（与排行榜面板同宽）
constexpr float kVolPadX         = 38.f;  // 面板左右内边距
constexpr float kVolPadTop       = 34.f;  // 面板顶边距首行标签
constexpr float kVolPadBottom    = 30.f;  // 末行滑块底缘距面板底边
constexpr float kVolTrackH       = 16.f;  // 滑轨胶囊高度
constexpr float kVolThumbR       = 15.f;  // 滑块圆钮半径（直径 30，明显大于轨道便于抓取）
constexpr float kVolLabelToTrack = 46.f;  // 标签中心 -> 滑轨中心
constexpr float kVolRowGap       = 48.f;  // 滑轨中心 -> 下一行标签中心
// ===== 栏目页签行几何（与排行榜 DrawModeTabs 同款） =====
constexpr float kTabW      = 104.f; // 单个页签宽（4 个页签 + 3 道间距 = 452，与面板同宽布局）
constexpr float kTabH      = 48.f;  // 单个页签高
constexpr float kTabGap    = 12.f;  // 页签间距
constexpr float kTabTopPad = 26.f;  // 面板顶边到页签行的留白
constexpr float kTabContentGap = 14.f; // 页签行下缘到栏目内容区顶的间隔
} // namespace

void SettingsScene::DrawImGuiLayout() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();

    // ===== 面板几何：页签行 + 当前栏目内容 =====
    // 行数按栏目取（音量/枭王形象 3 行，参数调节取参数表行数），面板高度随行数精确计算；
    // 栏目内行布局保持分栏前不变：标签 -> (kVolLabelToTrack) -> 滑轨 -> (kVolRowGap) -> 下一行标签
    const int   rows       = (tab == Tab::Params) ? TuningParams::Count : 3;
    const float labelH     = kVolLabelSize + 4.f;
    const float rowPitch   = kVolLabelToTrack + kVolRowGap;
    const float contentTop = kTabTopPad + kTabH + kTabContentGap; // 内容区顶（相对 panelTop）
    const float panelH     = contentTop + kVolPadTop + labelH * 0.5f
                           + (float)rows * kVolLabelToTrack
                           + (float)(rows - 1) * kVolRowGap
                           + kVolThumbR + kVolPadBottom;
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
    UIBuilder::DrawCenteredText("游戏设置", panelX, panelTop - 34.f,
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

    // ===== 行几何（绘制与命中同源） =====
    auto rowLabelY = [&](int i) { return row1LabelY + (float)i * rowPitch; };
    auto rowTrackY = [&](int i) { return row1TrackY + (float)i * rowPitch; };

    // 把某条滑条控件绑到第 i 行：热区盖住整条滑轨 + 圆钮，拖动只把归一化位置交给 onPos
    // （按下即定位/跟随由 UISystem 的捕获语义保证：按下当帧也会派发一次 Drag）
    auto bindSlider = [&](const std::shared_ptr<UIWidget>& w, int i,
                          std::function<void(float)> onPos) {
        const float trackY = rowTrackY(i);
        w->SetRect(UIRect::FromBounds(trackLeft - kVolThumbR, trackY - kVolThumbR - 8.f,
                                      trackRight + kVolThumbR, trackY + kVolThumbR + 8.f));
        w->SetOnDrag([trackLeft, trackW, onPos](const UIEvent& e) {
            onPos(std::clamp((e.pointer.x - trackLeft) / trackW, 0.f, 1.f));
        });
    };

    // ===== 单行滑条的统一皮肤：分隔线 -> 标签/读数 -> 胶囊滑轨 -> 金币圆钮 =====
    // 音量与参数调节两个栏目共用同一段绘制（改样式只改这里），
    // 栏目分支只负责"这一行显示什么文字、绑定什么数值"
    auto drawSlider = [&](int i, const char* label, const char* value,
                          float norm, bool active, bool hovered) {
        const float labelY = rowLabelY(i);
        const float trackY = rowTrackY(i);
        const float thumbX = trackLeft + std::clamp(norm, 0.f, 1.f) * trackW;
        const bool  hot    = active || hovered; // 悬停/拖拽中整条滑轨与圆钮一起亮起

        // 行间分隔线（暗金，与榜单行分隔同款）
        if (i > 0) {
            const float gapTop    = trackY - rowPitch + kVolThumbR; // 上一行滑块底缘
            const float gapBottom = labelY - labelH * 0.5f;         // 本行标签顶缘
            const float divY      = (gapTop + gapBottom) * 0.5f;
            dl->AddLine(ImVec2(panelLeft + 50.f, divY), ImVec2(panelRight - 50.f, divY),
                        UITheme::ToImU32(UITheme::AmberDim, 0.5f));
        }

        // 标签（拖拽中的行高亮为金色）+ 右对齐读数
        drawText(label, panelLeft + kVolPadX, labelY, kVolLabelSize,
                 active ? UITheme::ToImU32(UITheme::Amber)
                        : UITheme::ToImU32(UITheme::Moonlight), false);
        drawText(value, panelRight - kVolPadX, labelY, kVolPctSize,
                 UITheme::ToImU32(UITheme::Amber), true);

        // 滑轨：暗槽胶囊（悬停提亮）+ 金色已填充胶囊
        dl->AddRectFilled(ImVec2(trackLeft, trackY - kVolTrackH * 0.5f),
                          ImVec2(trackRight, trackY + kVolTrackH * 0.5f),
                          UITheme::ToImU32({0.12f, 0.10f, 0.18f}, hot ? 1.0f : 0.9f),
                          kVolTrackH * 0.5f);
        if (thumbX - trackLeft > 1.f) {
            // 填充段圆角随宽度收缩，避免极小值时胶囊变形
            const float fillR = std::min(kVolTrackH * 0.5f, (thumbX - trackLeft) * 0.5f);
            dl->AddRectFilled(ImVec2(trackLeft, trackY - kVolTrackH * 0.5f),
                              ImVec2(thumbX, trackY + kVolTrackH * 0.5f),
                              UITheme::ToImU32(UITheme::Amber, 0.9f), fillR);
        }

        // 滑块圆钮：阴影 + 金色主体 + 内环高光（与金币 HUD 同款质感）
        const ImU32 thumbCol = active ? UITheme::ToImU32({1.0f, 0.87f, 0.52f})
                             : hot    ? UITheme::ToImU32(UITheme::Amber)
                                      : UITheme::ToImU32(UITheme::Amber, 0.95f);
        dl->AddCircleFilled(ImVec2(thumbX + 1.5f, trackY + 2.f), kVolThumbR,
                            ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)), 20);
        dl->AddCircleFilled(ImVec2(thumbX, trackY), kVolThumbR, thumbCol, 20);
        dl->AddCircle(ImVec2(thumbX, trackY), kVolThumbR - 4.5f,
                      UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 16, 2.0f);
    };

    // 只启用当前栏目的控件：隐藏栏目的热区一并禁用（避免误触，同分栏前惯例）
    auto enableOnly = [&](bool volume, bool params, bool art, bool autoPlay) {
        for (int i = 0; i < 3; ++i) sliders[i]->SetEnabled(volume);
        for (int i = 0; i < TuningParams::Count; ++i) paramSliders[i]->SetEnabled(params);
        artToggle->SetEnabled(art);
        autoPlayToggle->SetEnabled(autoPlay);
    };

    if (tab == Tab::Volume) {
        // ===== 音量栏目：三行音量数据（UI 与分栏前一致） =====
        static const char* labels[3] = { "全局音量", "背景音乐", "音效" };
        float* values[3] = { &masterVolume, &bgmVolume, &sfxVolume };
        enableOnly(true, false, false, false);

        for (int i = 0; i < 3; ++i) {
            // 拖拽交互登记：热区与滑轨几何同源，判定与派发归 UISystem
            bindSlider(sliders[i], i, [this, i](float t) {
                float& v = VolumeRef(i);
                if (t == v) return;
                v = t;
                ApplyVolume(i, t);
            });

            char pct[16];
            snprintf(pct, sizeof(pct), "%.0f%%", *values[i] * 100.f);
            drawSlider(i, labels[i], pct, *values[i],
                       sliders[i]->IsCaptured(), sliders[i]->IsHovered());
        }
    } else if (tab == Tab::Params) {
        // ===== 参数调节栏目：读取并调整 TuningParams（拖动即生效并持久化） =====
        // 行标签/范围/量化/读数格式全部来自参数表；此处只做"位置 -> 归一化 -> 参数对象"
        enableOnly(false, true, false, false);
        TuningParams& tuning = svc_.Tuning();

        for (int i = 0; i < TuningParams::Count; ++i) {
            bindSlider(paramSliders[i], i, [&tuning, i](float t) {
                tuning.SetNormalized(i, t); // 量化、钳位与落盘都在参数对象内
            });

            char value[24];
            TuningParams::FormatValue(value, sizeof(value), i, tuning.Value(i));
            drawSlider(i, TuningParams::SpecOf(i).label, value, tuning.Normalized(i),
                       paramSliders[i]->IsCaptured(), paramSliders[i]->IsHovered());
        }
    } else if (tab == Tab::OwlArt) {
        // ===== 枭王形象栏目：分段切换控件原样保留，移入面板内容区垂直居中 =====
        enableOnly(false, false, true, false);

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
    } else {
        // ===== 自动演示栏目：开启/关闭分段开关（直连 AutoPilotDirector，切换即落盘）=====
        enableOnly(false, false, false, true);

        const float segW      = kVolPanelW - 2.f * kVolPadX;
        const float segH      = 44.f;
        const float contentCy = (panelTop + contentTop + panelBottom - kVolPadBottom) * 0.5f;
        const float segY      = contentCy + 4.f; // 胶囊中心（下方还要一行说明文字）
        const float segLeft   = panelLeft + kVolPadX;
        const float segRight  = segLeft + segW;
        const float segMid    = segLeft + segW * 0.5f;
        autoPlayToggle->SetRect(UIRect::FromBounds(segLeft, segY - segH * 0.5f,
                                                   segRight, segY + segH * 0.5f));
        const bool enabled = svc_.AutoPilot().Enabled();
        const bool segHot = autoPlayToggle->IsHovered() || autoPlayToggle->IsCaptured();

        // 小节标签（左对齐，同音量行标签字号）
        drawText("自动演示", panelLeft + kVolPadX, segY - segH * 0.5f - 22.f, kVolPctSize,
                 UITheme::ToImU32(UITheme::Moonlight), false);

        // 两段胶囊：整条暗槽底板 + 选中段金色浮层 + 分段文字（样式同枭王形象分段）
        dl->AddRectFilled(ImVec2(segLeft, segY - segH * 0.5f), ImVec2(segRight, segY + segH * 0.5f),
                          UITheme::ToImU32({0.12f, 0.10f, 0.18f}, segHot ? 1.0f : 0.9f), segH * 0.5f);
        {
            const float x0 = enabled ? segMid : segLeft;
            const float x1 = enabled ? segRight : segMid;
            const float fillR = std::min(segH * 0.5f, (x1 - x0) * 0.5f);
            dl->AddRectFilled(ImVec2(x0, segY - segH * 0.5f), ImVec2(x1, segY + segH * 0.5f),
                              UITheme::ToImU32(UITheme::Amber, 0.92f), fillR);
        }
        auto segText = [&](const char* str, float cx, bool sel) {
            const ImU32 col = sel ? UITheme::ToImU32({0.16f, 0.10f, 0.05f})
                                  : UITheme::ToImU32(UITheme::AmberDim, 0.9f);
            UIBuilder::DrawCenteredText(str, cx, segY, 20.f, col);
        };
        segText("关闭", segLeft + segW * 0.25f, !enabled);
        segText("开启", segMid + segW * 0.25f, enabled);

        // 生效范围说明（分段下方一行，居中）
        UIBuilder::DrawCenteredText("开启后：经典模式开局由 AI 自动操控蝙蝠",
                                    panelX, segY + segH * 0.5f + 26.f,
                                    kVolPctSize, UITheme::ToImU32(UITheme::AmberDim, 0.85f));
    }
}

// ===== 栏目页签行：音量设置 / 参数调节 / 枭王形象 / 自动演示（当前栏目高亮；样式与排行榜 DrawModeTabs 同款） =====
void SettingsScene::DrawModeTabs(float panelX, float panelTop) {
    const float rowY = panelTop + kTabTopPad + kTabH * 0.5f;
    // 页签序号即 Tab 枚举值：新增栏目只需在枚举与下表各加一项
    static const char* const kTabNames[TabCount] = { "音量设置", "参数调节", "枭王形象", "自动演示" };

    for (int i = 0; i < TabCount; ++i) {
        // 页签以 panelX 为中心对称排布（栏数变化时自动均分）
        const float tx = panelX + ((float)i - (float)(TabCount - 1) * 0.5f) * (kTabW + kTabGap);
        const bool selected = ((Tab)i == tab);

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
    const float rowW = (float)TabCount * kTabW + (float)(TabCount - 1) * kTabGap;
    const float lineY = rowY + kTabH * 0.5f + 8.f;
    dl->AddLine(ImVec2(panelX - rowW * 0.5f, lineY), ImVec2(panelX + rowW * 0.5f, lineY),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f));
}
