#include "app/Services.h"
#include "scene/ScoreRankScene.h"
#include "sprite/ButtonSprite.h"
#include "sprite/Sprite.h"
#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "ui/UISystem.h"
#include "ui/FontManager.h"
#include "res/ResourceManager.h"
#include "core/Config.h"
#include "core/Input.h"
#include "core/Util.h"
#include "app/ScoreRank.h"
#include "audio/AudioSystem.h"
#include "gameplay/ModeRegistry.h"
#include "layer/ToastLayer.h"
#include "scene/SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {
// 模式页签行几何（页签绘制与 UISystem 命中矩形、榜单视口裁剪共用）
constexpr float kTabW = 138.f;      // 单个页签宽
constexpr float kTabH = 48.f;       // 单个页签高
constexpr float kTabGap = 12.f;     // 页签间距
constexpr float kTabTopPad = 26.f;  // 面板顶边到页签行的留白

// 第 i 个页签相对行中心的偏移，单位为「页签宽 + 间距」。
// n=3 时等于 (i-1)，与原公式一致；模式数变化时自动保持对称。
inline float TabOffsetX(int i) {
    return (float)i - ((float)ModeRegistry::Instance().Size() - 1.0f) * 0.5f;
}

// 页签行总宽（分隔线宽度与之对齐），由模式数量推导
inline float TabRowWidth() {
    return (float)ModeRegistry::Instance().Size() * kTabW +
           (float)(ModeRegistry::Instance().Size() - 1) * kTabGap;
}
}

// =============================================================================
// 排行榜视图：模式页签行 + 榜单列表（含滚动/滑块/渐变遮罩）+ 标题文字。
//
// 只读 scoreRank / viewMode / listScroll 等场景状态，不发起写操作；
// 页签热区矩形与绘制几何同源（同一份 TabOffsetX / kTabW）。
// =============================================================================

void ScoreRankScene::DrawModeTabs(float panelX, float panelTop) {
    const float rowY = panelTop + kTabTopPad + kTabH * 0.5f;

    for (int i = 0; i < (int)ModeRegistry::Instance().Size(); ++i) {
        const float tx = panelX + TabOffsetX(i) * (kTabW + kTabGap);
        const bool selected = (viewMode == ModeRegistry::Instance().At((size_t)i).Key());

        // 热区与页签底板同源；选中项不可点（避免无效高亮与音效）
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
        UIBuilder::DrawCenteredText(ScoreRank::ModeName(ModeRegistry::Instance().At((size_t)i).Key()),
                                    tx, rowY, 22.f,
            selected ? UITheme::ToImU32(UITheme::Amber)
                     : UITheme::ToImU32(UITheme::Moonlight, hovered ? 0.95f : 0.75f));
        // 页签切换经 tabs[i] 的 onClick 回调（选中项已禁用，不会触发）
    }

    // 页签行与榜单之间的分隔线（宽度与页签行对齐）
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float halfRowW = TabRowWidth() * 0.5f;
    const float lineY = rowY + kTabH * 0.5f + 8.f;
    dl->AddLine(ImVec2(panelX - halfRowW, lineY), ImVec2(panelX + halfRowW, lineY),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f));
}

void ScoreRankScene::DrawImGuiRank() {
    const float WIN_WIDTH  = (float)svc_.Cfg().GetScreenWidth();

    // 榜单面板（暗色圆角 + 暗金描边）
    const float panelX = WIN_WIDTH * 0.5f;
    const float panelY = 530.f;
    const float panelW = 460.f;
    const float panelH = 540.f;
    UIBuilder::DrawRoundedRect(panelX, panelY, panelW, panelH, 16.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // 模式页签行（面板顶部）：三个模式独立榜单切换
    const float panelTop = panelY - panelH * 0.5f;
    DrawModeTabs(panelX, panelTop);

    if (scoreRank.empty()) {
        listZone->SetEnabled(false);
        scrollTrack->SetEnabled(false);
        scrollThumb->SetEnabled(false);
        UIBuilder::DrawCenteredText("暂无记录，快去创造第一个纪录吧！",
                                    panelX, panelY, 24.0f,
                                    UITheme::ToImU32(UITheme::Moonlight));
        return;
    }

    // ===== 布局常量 =====
    const float lineHeight  = 46.f;
    const float panelLeft   = panelX - panelW * 0.5f;
    const float panelRight  = panelX + panelW * 0.5f;
    const float panelBottom = panelY + panelH * 0.5f;

    // 滚动视口：模式页签行下方开始，底部内缩留边，右侧留出滑块轨道空间
    const float viewTop    = panelTop + kTabTopPad + kTabH + 14.f;
    const float viewBottom = panelBottom - 26.f;
    const float viewH      = viewBottom - viewTop;
    const float contentH   = (float)scoreRank.size() * lineHeight;
    const float maxScroll  = std::max(0.f, contentH - viewH);
    const bool  scrollable = maxScroll > 0.5f;

    // 滑块几何（仅 scrollable 时有意义）
    const float trackX     = panelRight - 18.f; // 轨道/滑块中心 x
    const float trackTop   = viewTop + 2.f;
    const float trackH     = viewH - 4.f;
    const float thumbH     = scrollable ? std::min(trackH, std::max(36.f, viewH * viewH / contentH)) : 0.f;
    const float thumbRange = std::max(1.f, trackH - thumbH); // 滑块可移动范围

    ImGuiIO& io = ImGui::GetIO();

    // 滑块顶部（拖拽/翻页/绘制共用）；不可滚动时退化为轨道顶，避免除零
    const float thumbTop = scrollable ? trackTop + (listScroll / maxScroll) * thumbRange : trackTop;
    const bool  dragging = scrollThumb->IsCaptured();

    // ===== 滚动交互登记：热区几何与绘制同源，命中判定与派发归 UISystem =====
    // 悬停面板任意位置即可滚轮滚动（一步约两行）
    listZone->SetRect(UIRect::FromBounds(panelLeft, panelTop, panelRight, panelBottom));
    listZone->SetEnabled(scrollable);
    listZone->SetOnWheel([this, lineHeight](const UIEvent& e) {
        listScrollTarget -= e.wheel * lineHeight * 2.f;
    });

    // 滑轨空白区：点击向点击方向翻一页
    scrollTrack->SetRect(UIRect::FromBounds(trackX - 11.f, trackTop,
                                           trackX + 11.f, trackTop + trackH));
    scrollTrack->SetEnabled(scrollable);
    scrollTrack->SetOnClick([this, thumbTop, viewH]() {
        listScrollTarget += (svc_.UI().GetPointer().y < thumbTop) ? -viewH : viewH;
    });

    // 滑块：命中区略大于绘制宽度便于抓取；按下即捕获，拖拽直接定位（不做平滑）
    scrollThumb->SetRect(UIRect::FromBounds(trackX - 9.f, thumbTop - 5.f,
                                           trackX + 9.f, thumbTop + thumbH + 5.f));
    scrollThumb->SetEnabled(scrollable);
    scrollThumb->SetOnPress([this, thumbTop](const UIEvent& e) {
        scrollbarGrabOffset = e.downPointer.y - thumbTop;
    });
    scrollThumb->SetOnDrag([this, trackTop, thumbRange, maxScroll](const UIEvent& e) {
        const float newTop = e.pointer.y - scrollbarGrabOffset;
        listScroll = listScrollTarget = (newTop - trackTop) / thumbRange * maxScroll;
    });

    // 收敛到合法区间（列表刷新后变短同样生效）；滚轮/翻页做帧率无关的平滑趋近
    listScrollTarget = std::clamp(listScrollTarget, 0.f, maxScroll);
    if (scrollable && !dragging) {
        const float t = 1.f - std::exp(-14.f * io.DeltaTime);
        listScroll += (listScrollTarget - listScroll) * t;
        if (std::fabs(listScrollTarget - listScroll) < 0.5f) listScroll = listScrollTarget;
    }
    listScroll = std::clamp(listScroll, 0.f, maxScroll);

    // ===== 榜单行：前三名金色大字，其余月光白；超出视口的行被裁剪 =====
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(panelLeft, viewTop), ImVec2(panelRight, viewBottom), true);

    for (size_t i = 0; i < scoreRank.size(); ++i) {
        const float y = viewTop + lineHeight * 0.5f + (float)i * lineHeight - listScroll;
        if (y < viewTop - lineHeight || y > viewBottom + lineHeight) continue; // 视口外整行剔除

        const bool isTop3 = (i < 3);
        const float rankSize  = isTop3 ? 30.0f : 24.0f;
        const float scoreSize = isTop3 ? 34.0f : 26.0f;

        // 名次（左对齐）
        std::string rankStr = std::to_string(i + 1);
        if (rankStr.size() < 2) rankStr = "0" + rankStr;
        std::string rankText = "No." + rankStr;
        ImU32 color = isTop3 ? UITheme::ToImU32(UITheme::Amber) : UITheme::ToImU32(UITheme::Moonlight);

        ImVec2 rts = UIBuilder::CalcTextSize(rankText, rankSize);
        ImVec2 rpos(panelLeft + 60.f, y - rts.y * 0.5f);
        dl->AddText(UIBuilder::GetFont(), rankSize, ImVec2(rpos.x + 2, rpos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), rankText.c_str());
        dl->AddText(UIBuilder::GetFont(), rankSize, rpos, color, rankText.c_str());

        // 分数（右对齐）
        std::string scoreStr = std::to_string(scoreRank[i]);
        ImVec2 sts = UIBuilder::CalcTextSize(scoreStr, scoreSize);
        ImVec2 spos(panelRight - 60.f - sts.x, y - sts.y * 0.5f);
        dl->AddText(UIBuilder::GetFont(), scoreSize, ImVec2(spos.x + 2, spos.y + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), scoreStr.c_str());
        dl->AddText(UIBuilder::GetFont(), scoreSize, spos, color, scoreStr.c_str());

        // 行分隔线
        if (i + 1 < scoreRank.size()) {
            float lineY = y + lineHeight * 0.5f;
            dl->AddLine(ImVec2(panelLeft + 50.f, lineY),
                        ImVec2(panelRight - 50.f, lineY),
                        UITheme::ToImU32(UITheme::AmberDim, 0.5f));
        }
    }
    dl->PopClipRect();

    if (!scrollable) {
        UIBuilder::DrawCenteredText("共 " + std::to_string(scoreRank.size()) + " 条记录",
                                    panelX, panelBottom - 13.f, 18.f,
                                    UITheme::ToImU32(UITheme::Moonlight, 0.60f));
        return;
    }

    // ===== 上下边缘渐隐（面板底色向透明的过渡，暗示可继续滚动） =====
    const float fadeH = 18.f;
    dl->AddRectFilledMultiColor(
        ImVec2(panelLeft, viewTop), ImVec2(panelRight, viewTop + fadeH),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f), UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),  UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f));
    dl->AddRectFilledMultiColor(
        ImVec2(panelLeft, viewBottom - fadeH), ImVec2(panelRight, viewBottom),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),  UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f), UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f));

    // ===== 右侧滑块：轨道 + 滑块（金色，hover 提亮、拖拽最亮） =====
    const float curTop = thumbTop;
    const bool thumbHot = dragging || scrollThumb->IsHovered();

    dl->AddRectFilled(ImVec2(trackX - 3.f, trackTop), ImVec2(trackX + 3.f, trackTop + trackH),
                      UITheme::ToImU32(UITheme::AmberDim, 0.30f), 3.0f);
    const ImU32 thumbCol = dragging ? UITheme::ToImU32({1.0f, 0.87f, 0.52f})
                          : thumbHot         ? UITheme::ToImU32(UITheme::Amber)
                                             : UITheme::ToImU32(UITheme::Amber, 0.85f);
    dl->AddRectFilled(ImVec2(trackX - 4.5f, curTop), ImVec2(trackX + 4.5f, curTop + thumbH),
                      thumbCol, 4.5f);

    // ===== 底部记录数 + 操作提示 =====
    UIBuilder::DrawCenteredText(
        "共 " + std::to_string(scoreRank.size()) + " 条记录 | 滚轮或拖动滑块查看",
        panelX, panelBottom - 13.f, 18.f,
        UITheme::ToImU32(UITheme::Moonlight, 0.60f));
}

void ScoreRankScene::DrawImGUIText() {
    // 兼容旧接口：转调 DrawImGuiRank
    DrawImGuiRank();
}

// ===== 清空记录二次确认弹窗 =====
