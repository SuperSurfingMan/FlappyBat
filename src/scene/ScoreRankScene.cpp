#include "ScoreRankScene.h"
#include "../sprite/ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../ui/UISystem.h"
#include "../res/ResourceManager.h"
#include "../sprite/Sprite.h"
#include "../Config.h"
#include "../app/Application.h"
#include "../app/ScoreRank.h"
#include "../font/FontManager.h"
#include "../layer/ToastLayer.h"
#include "../Input.h"
#include "../audio/AudioSystem.h"
#include "../Util.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {
// 模式页签行几何（页签绘制与 UISystem 命中矩形、榜单视口裁剪共用）
constexpr float kTabW = 138.f;      // 单个页签宽
constexpr float kTabH = 48.f;       // 单个页签高
constexpr float kTabGap = 12.f;     // 页签间距
constexpr float kTabTopPad = 26.f;  // 面板顶边到页签行的留白

// UI 层叠：榜单主控件组与确认弹窗（模态）
constexpr int kLayerRankMain = 20;
constexpr int kLayerConfirmDialog = 60;
}

ScoreRankScene::ScoreRankScene() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // UI 控件组：榜单主控件（非模态）+ 确认弹窗（模态）
    // 注册顺序即层叠顺序：后注册者在上（页签/滑块 > 滚轮区/滑轨 > 遮罩 > 按钮）
    UISystem& ui = UISystem::Get();
    rankGroupId   = ui.CreateGroup(kLayerRankMain);
    dialogGroupId = ui.CreateGroup(kLayerConfirmDialog);

    // 背景：钟楼内景（优先 v2，失败回退旧背景）
    auto bgTex = ResourceManager::Get().GetTexture2D("v2_ui_scoreboard_bg");
    if (!bgTex) bgTex = ResourceManager::Get().GetTexture2D("scoreRankBackground");
    background = std::make_shared<Sprite>(bgTex);
    background->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
    background->SetScale(WIN_WIDTH, WIN_HEIGHT);

    // 标题艺术字 HIGH SCORES
    if (auto tex = ResourceManager::Get().GetTexture2D("v2_ui_text_rank")) {
        titleSprite = std::make_shared<Sprite>(tex);
        titleSprite->SetLumaKey(true, 0.06f, 0.12f);
        titleSprite->SetScale(460.f, 230.f);
        titleSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT - 170.f); // GL 坐标 y 向上，标题置顶
    }

    // 底部双按钮并排：返回主菜单（主按钮）+ 清空记录（危险按钮）（ImGui 坐标 y 向下）
    backToReady = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f - 106.f, WIN_HEIGHT - 90.f},
        Vec2{290.f, UITheme::MainBtnH},
        "返回主菜单");
    backToReady->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
    });
    ui.Add(rankGroupId, backToReady.get());

    clearRecords = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Danger,
        Vec2{WIN_WIDTH * 0.5f + 158.f, WIN_HEIGHT - 90.f},
        Vec2{186.f, UITheme::MainBtnH},
        "清空记录");
    clearRecords->SetOnClick([this](){
        if (scoreRank.empty()) {
            auto toast = std::static_pointer_cast<ToastLayer>(toastLayer);
            toast->SetText("暂无记录可清空");
            toast->SetColor(Vec3{0.941f, 0.722f, 0.314f});
            toast->Activate();
            return;
        }
        OpenClearConfirm();
    });
    ui.Add(rankGroupId, clearRecords.get());

    // 右上角小图标：重新加载（ImGui 坐标 y 向下）
    syncAndLoad = std::make_shared<ButtonSprite>(
        UIBuilder::IconKind::Restart,
        Vec2{WIN_WIDTH - 52.f, 52.f},
        52.f);
    syncAndLoad->SetOnClick([this](){
        scoreRank = ScoreRank::Get().GetScoresFromDisk(viewMode);
        auto toast_cast = std::static_pointer_cast<ToastLayer>(toastLayer);
        toast_cast->SetText("已加载最新记录");
        toast_cast->SetColor(Vec3{0.4f, 0.85f, 0.4f});
        toast_cast->Activate();
    });
    ui.Add(rankGroupId, syncAndLoad.get());

    // Toast（顶部居中）
    Vec2 toastPosition = {WIN_WIDTH * 0.5f, 60.f};
    toastLayer = std::make_shared<ToastLayer>(2.0f, "已加载最新记录", Vec3{0.4f, 0.85f, 0.4f}, 24.0f, toastPosition);

    // 确认弹窗内的两个按钮（仅在弹窗打开时激活）
    float dialogCx, dialogCy, dialogW, dialogH;
    ConfirmDialogRect(dialogCx, dialogCy, dialogW, dialogH);

    confirmNo = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Secondary,
        Vec2{dialogCx - 85.f, dialogCy + 86.f},
        Vec2{160.f, 72.f},
        "取消");
    confirmNo->SetOnClick([this](){
        CloseClearConfirm();
    });

    confirmYes = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Danger,
        Vec2{dialogCx + 85.f, dialogCy + 86.f},
        Vec2{160.f, 72.f},
        "确认清空");
    confirmYes->SetOnClick([this](){
        // 一次性清空当前模式内存榜单并同步写入本地 score.dat，随后重新读盘确认结果
        ScoreRank::Get().ClearScores(viewMode);
        scoreRank = ScoreRank::Get().GetScoresFromDisk(viewMode);
        listScroll = listScrollTarget = 0.f;
        CloseClearConfirm();
        auto toast = std::static_pointer_cast<ToastLayer>(toastLayer);
        toast->SetText(std::string("已清空") + ScoreRank::ModeName(viewMode) + "记录");
        toast->SetColor(Vec3{0.870f, 0.360f, 0.360f});
        toast->Activate();
    });

    // ===== 内联热区控件登记（矩形与几何在绘制函数中与绘制同源同步） =====
    // 注册顺序决定层叠：先滚轮区（最大面积、最下层），再滑轨，最后滑块（优先被抓取）
    listZone.SetPlayFeedbackSFX(false);      // 纯滚轮热区：不发悬停/点击声
    ui.Add(rankGroupId, &listZone);

    scrollTrack.SetPlayFeedbackSFX(false);
    ui.Add(rankGroupId, &scrollTrack);

    scrollThumb.SetPlayFeedbackSFX(false);
    scrollThumb.SetCaptureOnPress(true);     // 按下即捕获：拖拽移出滑轨仍连续跟随
    ui.Add(rankGroupId, &scrollThumb);

    for (int i = 0; i < (int)ScoreRank::ModeCount; ++i) {
        tabs[i].SetOnClick([this, i]() { SwitchViewMode((ScoreRank::RankMode)i); });
        ui.Add(rankGroupId, &tabs[i]);       // 页签最后注册：盖在滚轮区之上
    }

    // 弹窗层叠（后注册者在上）：遮罩 > 面板本体（吞掉面板内空白点击）> 取消/确认按钮
    scrimZone.SetPlayFeedbackSFX(false);
    scrimZone.SetOnClick([this]() { CloseClearConfirm(); });
    ui.Add(dialogGroupId, &scrimZone);

    dialogBody.SetPlayFeedbackSFX(false);
    ui.Add(dialogGroupId, &dialogBody);

    ui.Add(dialogGroupId, confirmNo.get());
    ui.Add(dialogGroupId, confirmYes.get());

    confirmNo->SetShown(false);
    confirmYes->SetShown(false);
    scrimZone.SetEnabled(false);
    tabs[0].SetEnabled(false); // 选中页签不可点，后续由 DrawModeTabs 逐帧刷新
}

ScoreRankScene::~ScoreRankScene() {}

void ScoreRankScene::OnEnter() {
    viewMode = ScoreRank::RankMode::Classic; // 每次进入默认查看经典模式页签
    LoadRankList();
    // 每次进入榜单回到顶部
    listScroll = listScrollTarget = 0.f;

    UISystem& ui = UISystem::Get();
    ui.SetGroupEnabled(rankGroupId, true);
    ui.SetGroupEnabled(dialogGroupId, false);
}

void ScoreRankScene::OnExit() {
    CloseClearConfirm();
    scoreRank.clear();
    UISystem& ui = UISystem::Get();
    ui.SetGroupEnabled(rankGroupId, false);
    ui.SetGroupEnabled(dialogGroupId, false);
}

void ScoreRankScene::Update(float dt) {
    background->Update(dt);

    // 弹窗开合：模态压栈/弹栈 + 组开关（下层控件由模态自动让位，无需逐个挂起）
    UISystem& ui = UISystem::Get();
    ui.SetGroupEnabled(dialogGroupId, confirmClearOpen);
    if (confirmClearOpen) ui.PushModal(dialogGroupId);
    else ui.PopModal(dialogGroupId);

    if (confirmClearOpen) {
        // 弹窗打开：仅淡入推进与弹窗按钮动画；榜单滚动在绘制侧因控件禁用而自然挂起
        confirmFade = std::min(1.f, confirmFade + dt * 6.f);
    }

    // 按钮动画统一推进（Sprite::Update 对未激活者自动 no-op）
    backToReady->Update(dt);
    syncAndLoad->Update(dt);
    clearRecords->Update(dt);
    confirmYes->Update(dt);
    confirmNo->Update(dt);

    toastLayer->Update(dt);
}

void ScoreRankScene::Render(SpriteRenderer& renderer) {
    renderer.Draw_UV(background);
    if (titleSprite) renderer.Draw_UV(titleSprite);

    DrawImGuiRank();

    backToReady->DrawLayout();
    syncAndLoad->DrawLayout();
    clearRecords->DrawLayout();

    toastLayer->Render(renderer);

    // 确认弹窗最后绘制，覆盖在场景 UI 之上
    if (confirmClearOpen) {
        DrawClearConfirmDialog();
        confirmNo->DrawLayout();
        confirmYes->DrawLayout();
    }
}

void ScoreRankScene::Reset() {
    CloseClearConfirm();
    backToReady->Reset();
    background->Reset();
    syncAndLoad->Reset();
    clearRecords->Reset();
    confirmYes->Reset();
    confirmNo->Reset();
    toastLayer->Reset();
    scoreRank.clear();
    viewMode = ScoreRank::RankMode::Classic;
    listScroll = listScrollTarget = 0.f;
    UISystem::Get().ClearWidgetState(rankGroupId);
    UISystem::Get().ClearWidgetState(dialogGroupId);
}

void ScoreRankScene::LoadRankList() {
    scoreRank = ScoreRank::Get().GetScores(viewMode);
}

void ScoreRankScene::SwitchViewMode(ScoreRank::RankMode mode) {
    if (mode == viewMode) return;
    viewMode = mode;
    scoreRank = ScoreRank::Get().GetScores(viewMode);
    listScroll = listScrollTarget = 0.f; // 切换模式回到顶部
}

// ===== 模式页签行：三个模式独立榜单切换（当前模式高亮；弹窗打开时由模态组自动让位） =====
void ScoreRankScene::DrawModeTabs(float panelX, float panelTop) {
    const float rowY = panelTop + kTabTopPad + kTabH * 0.5f;

    for (int i = 0; i < (int)ScoreRank::ModeCount; ++i) {
        const float tx = panelX + (float)(i - 1) * (kTabW + kTabGap);
        const bool selected = (i == (int)viewMode);

        // 热区与页签底板同源；选中项不可点（避免无效高亮与音效）
        tabs[i].SetRect(UIRect::FromCenter(tx, rowY, kTabW, kTabH));
        tabs[i].SetEnabled(!selected);
        const bool hovered = tabs[i].IsHovered();

        // 底板：选中 = 主按钮紫 + 亮金描边；未选中 = 面板同款暗色 + 暗金描边（悬停提亮）
        UIBuilder::DrawRoundedRect(tx, rowY, kTabW, kTabH, 12.f,
            selected ? UITheme::ToImU32({0.243f, 0.169f, 0.396f}, 0.95f)
                     : UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
            selected ? UITheme::ToImU32(UITheme::Amber)
                     : UITheme::ToImU32(UITheme::AmberDim, hovered ? 0.95f : 0.55f),
            selected ? 3.0f : 2.0f);

        // 文字：选中金色，未选中月光白（悬停提亮）
        UIBuilder::DrawCenteredText(ScoreRank::ModeName((ScoreRank::RankMode)i),
                                    tx, rowY, 22.f,
            selected ? UITheme::ToImU32(UITheme::Amber)
                     : UITheme::ToImU32(UITheme::Moonlight, hovered ? 0.95f : 0.75f));
        // 页签切换经 tabs[i] 的 onClick 回调（选中项已禁用，不会触发）
    }

    // 页签行与榜单之间的分隔线（宽度与页签行对齐）
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float halfRowW = (3.f * kTabW + 2.f * kTabGap) * 0.5f;
    const float lineY = rowY + kTabH * 0.5f + 8.f;
    dl->AddLine(ImVec2(panelX - halfRowW, lineY), ImVec2(panelX + halfRowW, lineY),
                UITheme::ToImU32(UITheme::AmberDim, 0.5f));
}

void ScoreRankScene::DrawImGuiRank() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();

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
        listZone.SetEnabled(false);
        scrollTrack.SetEnabled(false);
        scrollThumb.SetEnabled(false);
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
    const bool  dragging = scrollThumb.IsCaptured();

    // ===== 滚动交互登记：热区几何与绘制同源，命中判定与派发归 UISystem =====
    // 悬停面板任意位置即可滚轮滚动（一步约两行）
    listZone.SetRect(UIRect::FromBounds(panelLeft, panelTop, panelRight, panelBottom));
    listZone.SetEnabled(scrollable);
    listZone.SetOnWheel([this, lineHeight](const UIEvent& e) {
        listScrollTarget -= e.wheel * lineHeight * 2.f;
    });

    // 滑轨空白区：点击向点击方向翻一页
    scrollTrack.SetRect(UIRect::FromBounds(trackX - 11.f, trackTop,
                                           trackX + 11.f, trackTop + trackH));
    scrollTrack.SetEnabled(scrollable);
    scrollTrack.SetOnClick([this, thumbTop, viewH]() {
        listScrollTarget += (UISystem::Get().GetPointer().y < thumbTop) ? -viewH : viewH;
    });

    // 滑块：命中区略大于绘制宽度便于抓取；按下即捕获，拖拽直接定位（不做平滑）
    scrollThumb.SetRect(UIRect::FromBounds(trackX - 9.f, thumbTop - 5.f,
                                           trackX + 9.f, thumbTop + thumbH + 5.f));
    scrollThumb.SetEnabled(scrollable);
    scrollThumb.SetOnPress([this, thumbTop](const UIEvent& e) {
        scrollbarGrabOffset = e.downPointer.y - thumbTop;
    });
    scrollThumb.SetOnDrag([this, trackTop, thumbRange, maxScroll](const UIEvent& e) {
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
    const bool thumbHot = dragging || scrollThumb.IsHovered();

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

void ScoreRankScene::ConfirmDialogRect(float& cx, float& cy, float& w, float& h) const {
    cx = (float)Config::GetInstance().GetScreenWidth() * 0.5f;
    cy = (float)Config::GetInstance().GetScreenHeight() * 0.49f;
    w = 420.f;
    h = 310.f;
}

void ScoreRankScene::OpenClearConfirm() {
    if (confirmClearOpen) return;
    confirmClearOpen = true;
    confirmFade = 0.f;
    // 场景按钮隐去（绘制与交互一并关闭）；弹窗内控件就绪，模态压栈由 Update 统一完成
    backToReady->SetShown(false);
    syncAndLoad->SetShown(false);
    clearRecords->SetShown(false);
    confirmYes->SetShown(true);
    confirmNo->SetShown(true);
    scrimZone.SetEnabled(true);
    dialogBody.SetEnabled(true);

    UISystem& ui = UISystem::Get();
    ui.SetGroupEnabled(dialogGroupId, true);
    ui.PushModal(dialogGroupId);
}

void ScoreRankScene::CloseClearConfirm() {
    confirmClearOpen = false;
    confirmFade = 0.f;
    confirmYes->SetShown(false);
    confirmNo->SetShown(false);
    scrimZone.SetEnabled(false);
    dialogBody.SetEnabled(false);
    backToReady->SetShown(true);
    syncAndLoad->SetShown(true);
    clearRecords->SetShown(true);

    UISystem& ui = UISystem::Get();
    ui.SetGroupEnabled(dialogGroupId, false);
    ui.PopModal(dialogGroupId);
}

void ScoreRankScene::DrawClearConfirmDialog() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    float cx, cy, dw, dh;
    ConfirmDialogRect(cx, cy, dw, dh);
    const float top = cy - dh * 0.5f;
    const float f = confirmFade; // 淡入进度

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 热区与遮罩/面板几何同源：遮罩整屏（点任意处取消），面板本体吞掉面板内空白点击
    scrimZone.SetRect(UIRect{0.f, 0.f, WIN_WIDTH, WIN_HEIGHT});
    dialogBody.SetRect(UIRect::FromCenter(cx, cy, dw, dh));

    // 全屏半透明遮罩：压暗并隔离背景 UI
    dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(WIN_WIDTH, WIN_HEIGHT),
                      ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.66f * f)));

    // 弹窗面板：与榜单面板同款暗色圆角 + 暗金描边（面板与文字不参与淡入，
    // 与不透明绘制的弹窗按钮保持一致；淡入仅作用于遮罩）
    UIBuilder::DrawRoundedRect(cx, cy, dw, dh, 16.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.97f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    // 标题 + 警示文案（确认/取消按钮由 Render 中的 DrawLayout 绘制）
    UIBuilder::DrawCenteredText("清空记录", cx, top + 50.f, 34.0f,
                                UITheme::ToImU32(UITheme::Amber));
    UIBuilder::DrawCenteredText("确定要清空该模式的所有历史记录吗？", cx, cy - 25.f, 26.0f,
                                UITheme::ToImU32(UITheme::Moonlight));
    UIBuilder::DrawCenteredText(std::string(ScoreRank::ModeName(viewMode)) + " · 共 " +
                                std::to_string(scoreRank.size()) + " 条记录 · 此操作不可恢复",
                                cx, cy + 22.f, 20.0f,
                                UITheme::ToImU32(UITheme::Blood));
}
