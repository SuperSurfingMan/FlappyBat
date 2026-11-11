#include "app/Services.h"
#include "scene/ReadyScene.h"
#include "scene/ReadySceneLayout.h"
#include "scene/SceneManager.h"
#include "core/Config.h"
#include "app/Application.h"
#include "app/PlayerProfile.h"
#include "res/ResourceManager.h"
#include "audio/AudioSystem.h"
#include "sprite/BatSprite.h"
#include "sprite/ButtonSprite.h"
#include "gameplay/ModeRegistry.h"
#include "gameplay/StageParams.h"
#include "ui/UIBuilder.h"
#include "ui/UISystem.h"
#include "ui/UITheme.h"
#include "imgui.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace {

// 布局常量集中一处（ReadySceneLayout.h），这里只补上"模式数量从注册表来"这一层推导
using namespace ready::layout;

float ModeOffset(int i) {
    return ModeOffsetX(i, ModeRegistry::Instance().Size());
}

} // namespace

// =============================================================================
// 护盾升级（B8）：右上角金币入口 + 模态升级面板。
//
// 热区矩形与底板几何逐帧同源（都在本文件内计算），点击收益结算走 PlayerProfile，
// 场景只负责"长什么样、在哪点"；金币不足的提示计时由场景状态承载。
// =============================================================================

void ReadyScene::DrawUpgradeEntryButton() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();

    // 奖杯图标（右上角，中心 WIN_WIDTH-52, 52）左侧的金币入口，与其同行贴顶对齐
    const float w = 176.0f, h = 44.0f;
    const float rightEdge = WIN_WIDTH - 52.0f - 26.0f - 10.0f; // 奖杯左缘再留 10px 间距
    const float cx = rightEdge - w * 0.5f;
    const float cy = 52.0f; // 与奖杯图标垂直居中对齐（贴屏幕顶部）

    // 命中矩形与底板几何同源；悬停高亮直读控件状态（由 UISystem 维护）
    coinEntry->SetRect(UIRect::FromCenter(cx, cy, w, h));
    const bool hovered = coinEntry->IsHovered();

    UIBuilder::DrawRoundedRect(cx, cy, w, h, h * 0.5f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(hovered ? UITheme::Amber : UITheme::AmberDim, 0.9f),
        hovered ? 3.0f : 2.0f);

    // 金币图标（双层金色圆，与局内金币 HUD 同款）
    const float icx = cx - w * 0.5f + 22.0f;
    dl->AddCircleFilled(ImVec2(icx + 1, cy + 1), 9.0f,
                        UITheme::ToImU32(UITheme::AmberDim, 0.6f), 16);
    dl->AddCircleFilled(ImVec2(icx, cy), 9.0f, UITheme::ToImU32(UITheme::Amber), 16);
    dl->AddCircle(ImVec2(icx, cy), 5.5f, UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 12, 1.5f);

    if (font) {
        // 持有数（图标右侧，金色）
        std::string coinStr = std::to_string(svc_.Profile().GetCoins());
        dl->AddText(font, 17.0f, ImVec2(icx + 15.0f + 1, cy - 9.0f + 1),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), coinStr.c_str());
        dl->AddText(font, 17.0f, ImVec2(icx + 15.0f, cy - 9.0f),
                    UITheme::ToImU32(UITheme::Amber), coinStr.c_str());

        // 「护盾升级」标签（右对齐）
        const char* label = "护盾升级";
        ImVec2 ls = UIBuilder::CalcTextSize(label, 16.0f);
        dl->AddText(font, 16.0f,
                    ImVec2(cx + w * 0.5f - 14.0f - ls.x + 1, cy - ls.y * 0.5f + 1),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), label);
        dl->AddText(font, 16.0f,
                    ImVec2(cx + w * 0.5f - 14.0f - ls.x, cy - ls.y * 0.5f),
                    UITheme::ToImU32(UITheme::Moonlight, hovered ? 0.95f : 0.6f), label);
    }
}

void ReadyScene::DrawUpgradePanelUI() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    const float WIN_WIDTH = (float)svc_.Cfg().GetScreenWidth();
    const float WIN_HEIGHT = (float)svc_.Cfg().GetScreenHeight();
    const float cx = WIN_WIDTH * 0.5f;

    auto& profile = svc_.Profile();

    // 全屏暗色遮罩（与模式选择覆盖层同款）
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(WIN_WIDTH, WIN_HEIGHT),
                      ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.05f, 0.78f)));

    // 标题 + 金币余额
    UIBuilder::DrawCenteredText("护盾升级", cx, 128.0f, 34.0f,
                                UITheme::ToImU32(UITheme::Amber));
    {
        const float pillW = 200.0f, pillH = 44.0f;
        const float pcy = 180.0f;
        UIBuilder::DrawRoundedRect(cx, pcy, pillW, pillH, pillH * 0.5f,
            UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.9f),
            UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);
        const float icx = cx - 58.0f;
        dl->AddCircleFilled(ImVec2(icx + 1, pcy + 1), 10.0f,
                            UITheme::ToImU32(UITheme::AmberDim, 0.6f), 16);
        dl->AddCircleFilled(ImVec2(icx, pcy), 10.0f, UITheme::ToImU32(UITheme::Amber), 16);
        dl->AddCircle(ImVec2(icx, pcy), 6.0f, UITheme::ToImU32({1.0f, 0.92f, 0.7f}), 12, 1.5f);
        UIBuilder::DrawCenteredText(std::to_string(profile.GetCoins()),
                                    cx + 14.0f, pcy, 22.0f,
                                    UITheme::ToImU32(UITheme::Amber));
    }

    // 金币不足提示（点击买不起的轨道时短暂显示）
    if (insufficientMsgTimer > 0.0f) {
        UIBuilder::DrawCenteredText("金币不足！", cx, 228.0f, 20.0f,
                                    UITheme::ToImU32(UITheme::Blood));
    }

    // 悬停/点击事实全部直读控件（矩形在下方与卡片几何同步），本函数不再参与判定

    // ===== 两条升级轨道卡片 =====
    // 展示数值与 PlayerProfile / MainScene 的升级参数保持一致：
    // 持续时间 3.0s + 0.4s/级；充能速度 ×1.0 + 10%/级
    static const char* trackNames[PlayerProfile::TrackCount] = { "持续时间", "充能速度" };
    static const char* trackDescs[PlayerProfile::TrackCount] = {
        "护盾无敌时间 · 每级 +0.4 秒 · 满级 5.0 秒",
        "能量获取效率 · 每级 +10% · 满级 ×1.5",
    };

    for (int t = 0; t < PlayerProfile::TrackCount; ++t) {
        const float cardW = 452.0f, cardH = 180.0f;
        const float cy = 330.0f + (float)t * 198.0f;
        const float top = cy - cardH * 0.5f;
        const float left = cx - cardW * 0.5f;
        const float right = cx + cardW * 0.5f;

        // 卡片底板（与模式选择卡片同款石板质感）
        UIBuilder::DrawButtonPlate(cx, cy, cardW, cardH, 14.0f,
            UITheme::ToImU32(UITheme::NightBg, 0.95f),
            UITheme::ToImU32(UITheme::NightDeep, 0.95f),
            UITheme::ToImU32(UITheme::AmberDim), 2.0f);

        const int lv = profile.GetLevel(t);
        const bool maxed = lv >= PlayerProfile::MaxLevel;
        const long long cost = profile.NextCost(t); // 满级时为 -1

        // 名称（左） + 当前值（右）
        if (font) {
            const float nameY = top + 34.0f;
            dl->AddText(font, 26.0f, ImVec2(left + 24.0f + 2, nameY - 13.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), trackNames[t]);
            dl->AddText(font, 26.0f, ImVec2(left + 24.0f, nameY - 13.0f),
                        UITheme::ToImU32(UITheme::Amber), trackNames[t]);

            char valBuf[32];
            if (t == PlayerProfile::ShieldTrack::Duration) {
                snprintf(valBuf, sizeof(valBuf), "%.1f 秒", 3.0f + 0.4f * (float)lv);
            } else {
                snprintf(valBuf, sizeof(valBuf), "×%.1f", 1.0f + 0.1f * (float)lv);
            }
            ImVec2 ts = UIBuilder::CalcTextSize(valBuf, 26.0f);
            dl->AddText(font, 26.0f, ImVec2(right - 24.0f - ts.x + 2, nameY - 13.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), valBuf);
            dl->AddText(font, 26.0f, ImVec2(right - 24.0f - ts.x, nameY - 13.0f),
                        UITheme::ToImU32(UITheme::Moonlight), valBuf);

            // 说明行
            dl->AddText(font, 16.0f, ImVec2(left + 24.0f + 2, top + 58.0f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.7f)), trackDescs[t]);
            dl->AddText(font, 16.0f, ImVec2(left + 24.0f, top + 58.0f),
                        UITheme::ToImU32(UITheme::Moonlight, 0.6f), trackDescs[t]);
        }

        // 等级刻度：5 个圆点（已升级的实心金色）
        const float pipSpacing = 30.0f;
        const float pipY = top + 98.0f;
        for (int i = 0; i < PlayerProfile::MaxLevel; ++i) {
            const float px = cx + ((float)i - (PlayerProfile::MaxLevel - 1) * 0.5f) * pipSpacing;
            if (i < lv) {
                dl->AddCircleFilled(ImVec2(px + 1, pipY + 1), 7.0f,
                                    UITheme::ToImU32(UITheme::AmberDim, 0.5f), 12);
                dl->AddCircleFilled(ImVec2(px, pipY), 7.0f,
                                    UITheme::ToImU32(UITheme::Amber), 12);
            } else {
                dl->AddCircleFilled(ImVec2(px, pipY), 7.0f,
                                    UITheme::ToImU32({0.05f, 0.04f, 0.09f}), 12);
                dl->AddCircle(ImVec2(px, pipY), 7.0f,
                              UITheme::ToImU32(UITheme::AmberDim, 0.6f), 12, 2.0f);
            }
        }

        // 购买按钮：可买（主按钮高亮）/ 买不起（压暗）/ 满级（置灰）
        const float btnW = 240.0f, btnH = 52.0f;
        const float bx = cx, by = top + 140.0f;
        buyZones[t]->SetRect(UIRect::FromCenter(bx, by, btnW, btnH)); // 热区与底板同源
        const bool affordable = !maxed && profile.GetCoins() >= cost;
        const bool btnHover = affordable && buyZones[t]->IsHovered();
        const std::string buyText = maxed ? "已满级"
                                    : "升级 · " + std::to_string(cost) + " 金币";

        if (maxed) {
            UIBuilder::DrawButtonPlate(bx, by, btnW, btnH, 14.0f,
                UITheme::ToImU32({0.10f, 0.09f, 0.14f}, 0.9f),
                UITheme::ToImU32(UITheme::NightDeep, 0.9f),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f), 2.0f);
            UIBuilder::DrawButtonText(buyText, bx, by, 22.0f, false, false);
        } else if (affordable) {
            UIBuilder::DrawButton(bx, by, btnW, btnH, true, btnHover, false);
            UIBuilder::DrawButtonText(buyText, bx, by, 20.0f, btnHover, false);
        } else {
            UIBuilder::DrawButtonPlate(bx, by, btnW, btnH, 14.0f,
                UITheme::ToImU32({0.12f, 0.10f, 0.16f}, 0.9f),
                UITheme::ToImU32(UITheme::NightDeep, 0.9f),
                UITheme::ToImU32(UITheme::AmberDim, 0.7f), 2.0f);
            if (font) {
                ImVec2 ts = UIBuilder::CalcTextSize(buyText, 20.0f);
                dl->AddText(font, 20.0f, ImVec2(bx - ts.x * 0.5f + 2, by - ts.y * 0.5f + 2),
                            ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), buyText.c_str());
                dl->AddText(font, 20.0f, ImVec2(bx - ts.x * 0.5f, by - ts.y * 0.5f),
                            UITheme::ToImU32(UITheme::Moonlight, 0.45f), buyText.c_str());
            }
        }
        // 购买点击（成功=奖励音并自动存盘；失败=短暂提示金币不足）已下沉到 buyZones[t] 的 onClick
    }

    // 返回按钮（关闭面板回到主菜单）
    const float backY = 672.0f;
    upgradeBackZone->SetRect(UIRect::FromCenter(cx, backY, 200.0f, 60.0f)); // 热区与底板同源
    const bool backHovered = upgradeBackZone->IsHovered();
    UIBuilder::DrawButton(cx, backY, 200.0f, 60.0f, false, backHovered, false);
    UIBuilder::DrawButtonText("返回", cx, backY, 24.0f, backHovered, false);
}
