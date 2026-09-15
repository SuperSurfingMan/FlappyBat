#include "SettingsScene.h"
#include "../sprite/ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../res/ResourceManager.h"
#include "../sprite/Sprite.h"
#include "../Config.h"
#include "../app/Application.h"
#include "../audio/AudioSystem.h"
#include "../Input.h"
#include "../scene/SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

SettingsScene::SettingsScene() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // 背景：复用排行榜场景的钟楼内景
    auto bgTex = ResourceManager::Get().GetTexture2D("v2_ui_scoreboard_bg");
    if (!bgTex) bgTex = ResourceManager::Get().GetTexture2D("scoreRankBackground");
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
    backToReady->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
    });
}

SettingsScene::~SettingsScene() {}

void SettingsScene::OnEnter() {
    // 进入时从音频系统读取当前音量到本地缓存（供滑块显示）
    SyncFromAudioSystem();
}

void SettingsScene::OnExit() {
    draggingSlider = -1; // 离开场景时终止拖拽状态
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
    draggingSlider = -1;
    SyncFromAudioSystem();
}

void SettingsScene::SyncFromAudioSystem() {
    masterVolume = AudioSystem::Instance().GetMasterVolume();
    bgmVolume    = AudioSystem::Instance().GetBGMVolume();
    sfxVolume    = AudioSystem::Instance().GetSFXVolume();
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
}

void SettingsScene::DrawImGuiLayout() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    const float mx = Input::GetMouseX();
    const float my = Input::GetMouseY();

    // ===== 标题 =====
    UIBuilder::DrawCenteredText("声音设置", WIN_WIDTH * 0.5f, WIN_HEIGHT - 230.f,
                                UITheme::FontH2, UITheme::ToImU32(UITheme::Amber));

    // ===== 面板几何：高度按三行控件精确计算，正好包住全部内容 =====
    // 行布局：标签 -> (kVolLabelToTrack) -> 滑轨 -> (kVolRowGap) -> 下一行标签
    const float labelH   = kVolLabelSize + 4.f;
    const float rowPitch = kVolLabelToTrack + kVolRowGap;
    const float panelH   = kVolPadTop + labelH * 0.5f + 3.f * kVolLabelToTrack
                         + 2.f * kVolRowGap + kVolThumbR + kVolPadBottom;
    const float panelX     = WIN_WIDTH * 0.5f;
    const float panelY     = WIN_HEIGHT * 0.5f;
    const float panelTop   = panelY - panelH * 0.5f;
    const float panelLeft  = panelX - kVolPanelW * 0.5f;
    const float panelRight = panelX + kVolPanelW * 0.5f;
    const float trackLeft  = panelLeft + kVolPadX;
    const float trackRight = panelRight - kVolPadX;
    const float trackW     = trackRight - trackLeft;
    const float row1LabelY = panelTop + kVolPadTop + labelH * 0.5f;
    const float row1TrackY = row1LabelY + kVolLabelToTrack;

    // 面板底板：暗色圆角 + 暗金描边（与排行榜面板/页签同款）
    UIBuilder::DrawRoundedRect(panelX, panelY, kVolPanelW, panelH, 16.f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // ===== 三行音量数据 =====
    const char* labels[3] = { "全局音量", "背景音乐", "音效" };
    float* values[3] = { &masterVolume, &bgmVolume, &sfxVolume };
    auto applyValue = [](int idx, float v) {
        switch (idx) {
            case 0: AudioSystem::Instance().SetMasterVolume(v); break;
            case 1: AudioSystem::Instance().SetBGMVolume(v);    break;
            case 2: AudioSystem::Instance().SetSFXVolume(v);    break;
        }
        AudioSystem::Instance().SaveSettings(); // 滑块改变即实时应用并持久化
    };

    // ===== 拖拽交互：点击轨道任意处跳转，按住后跟随鼠标，松开结束 =====
    if (Input::IsMousePressed(0)) {
        for (int i = 0; i < 3; ++i) {
            const float trackY = row1TrackY + (float)i * rowPitch;
            if (mx >= trackLeft - kVolThumbR && mx <= trackRight + kVolThumbR &&
                my >= trackY - kVolThumbR - 8.f && my <= trackY + kVolThumbR + 8.f) {
                draggingSlider = i;
                break;
            }
        }
    }
    if (draggingSlider >= 0) {
        if (Input::IsMouseDown(0)) {
            const float t = std::clamp((mx - trackLeft) / trackW, 0.f, 1.f);
            const int i = draggingSlider;
            if (t != *values[i]) {
                *values[i] = t;
                applyValue(i, t);
            }
        } else {
            draggingSlider = -1; // 松开结束拖拽
        }
    }

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

    // ===== 逐行绘制：分隔线 -> 标签/百分比 -> 胶囊滑轨 -> 金币圆钮 =====
    for (int i = 0; i < 3; ++i) {
        const float labelY = row1LabelY + (float)i * rowPitch;
        const float trackY = row1TrackY + (float)i * rowPitch;
        const float value  = *values[i];
        const float thumbX = trackLeft + value * trackW;
        const bool active  = (draggingSlider == i);

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
        const bool trackHot = active ||
            (mx >= trackLeft && mx <= trackRight &&
             std::fabs(my - trackY) <= kVolThumbR + 6.f);
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
        const bool thumbHot = active ||
            (std::fabs(mx - thumbX) <= kVolThumbR + 6.f &&
             std::fabs(my - trackY) <= kVolThumbR + 6.f);
        const ImU32 thumbCol = active   ? UITheme::ToImU32({1.0f, 0.87f, 0.52f})
                             : thumbHot ? UITheme::ToImU32(UITheme::Amber)
                                        : UITheme::ToImU32(UITheme::Amber, 0.95f);
        dl->AddCircleFilled(ImVec2(thumbX + 1.5f, trackY + 2.f), kVolThumbR,
                            ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)), 20);
        dl->AddCircleFilled(ImVec2(thumbX, trackY), kVolThumbR, thumbCol, 20);
        dl->AddCircle(ImVec2(thumbX, trackY), kVolThumbR - 4.5f,
                      UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 16, 2.0f);
    }
}
