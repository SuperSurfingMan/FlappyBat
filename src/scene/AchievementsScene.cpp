#include "AchievementsScene.h"
#include "../sprite/ButtonSprite.h"
#include "../ui/UIBuilder.h"
#include "../ui/UITheme.h"
#include "../ui/UISystem.h"
#include "../res/ResourceManager.h"
#include "../sprite/Sprite.h"
#include "../Config.h"
#include "../app/Application.h"
#include "../app/Achievements.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

AchievementsScene::AchievementsScene() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    const float WIN_HEIGHT = (float)Config::GetInstance().GetScreenHeight();

    // 背景：成就页专属背景（优先成就背景，失败回退榜单背景）
    auto bgTex = ResourceManager::Get().GetTexture2D("v2_ui_achievements_bg");
    background = std::make_shared<Sprite>(bgTex);
    background->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT * 0.5f);
    background->SetScale(WIN_WIDTH, WIN_HEIGHT);

    // 标题艺术字（成就页专属）
    if (auto tex = ResourceManager::Get().GetTexture2D("v2_ui_text_achievements")) {
        titleSprite = std::make_shared<Sprite>(tex);
        titleSprite->SetLumaKey(true, 0.06f, 0.12f);
        titleSprite->SetScale(460.f, 230.f);
        titleSprite->SetPosition(WIN_WIDTH * 0.5f, WIN_HEIGHT - 170.f); // GL 坐标 y 向上，标题置顶
    }

    // 底部按钮：返回主菜单（ImGui 坐标 y 向下）
    backToReady = std::make_shared<ButtonSprite>(
        ButtonSprite::Style::Primary,
        Vec2{WIN_WIDTH * 0.5f, WIN_HEIGHT - 90.f},
        Vec2{290.f, UITheme::MainBtnH},
        "返回主菜单");
    backToReady->SetOnClick([](){
        SceneManager::Instance().ChangeScene(Application::Get().GetScene(Application::Scenes::ReadyScene));
    });

    // ===== UI 控件组与热区登记（矩形在 DrawAchievements 中与绘制同源） =====
    // 注册顺序决定层叠：滚轮区（最大面积）< 滑轨 < 滑块（优先被抓取）
    UISystem& ui = UISystem::Get();
    achGroupId = ui.CreateGroup(20); // 与其它场景主控件同层
    ui.Add(achGroupId, backToReady);

    listZone->SetPlayFeedbackSFX(false);
    ui.Add(achGroupId, listZone);

    scrollTrack->SetPlayFeedbackSFX(false);
    ui.Add(achGroupId, scrollTrack);

    scrollThumb->SetPlayFeedbackSFX(false);
    scrollThumb->SetCaptureOnPress(true);
    ui.Add(achGroupId, scrollThumb);
}

AchievementsScene::~AchievementsScene() {}

void AchievementsScene::OnEnter() {
    // 每次进入成就列表回到顶部
    achScroll = achScrollTarget = 0.f;
    UISystem::Get().SetGroupEnabled(achGroupId, true);
}

void AchievementsScene::OnExit() {
    UISystem::Get().SetGroupEnabled(achGroupId, false);
}

void AchievementsScene::Update(float dt) {
    background->Update(dt);
    backToReady->Update(dt);
}

void AchievementsScene::Render(SpriteRenderer& renderer) {
    renderer.Draw_UV(background);
    if (titleSprite) renderer.Draw_UV(titleSprite);

    DrawAchievements();

    backToReady->DrawLayout();
}

void AchievementsScene::Reset() {
    achScroll = achScrollTarget = 0.f;
    backToReady->Reset();
    background->Reset();
    UISystem::Get().ClearWidgetState(achGroupId);
}

// ===== 成就列表（B7）：与榜单同款的可滚动列表（滚轮/滑块拖拽/轨道翻页） =====
void AchievementsScene::DrawAchievements() {
    const float WIN_WIDTH  = (float)Config::GetInstance().GetScreenWidth();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;

    auto& sys = Achievements::Get();
    auto& defs = Achievements::Defs();

    // 与榜单同几何的面板
    const float panelX = WIN_WIDTH * 0.5f;
    const float panelY = 530.f;
    const float panelW = 460.f;
    const float panelH = 540.f;
    UIBuilder::DrawRoundedRect(panelX, panelY, panelW, panelH, 16.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32(UITheme::AmberDim, 0.9f), 2.0f);

    const float panelLeft   = panelX - panelW * 0.5f;
    const float panelRight  = panelX + panelW * 0.5f;
    const float panelTop    = panelY - panelH * 0.5f;
    const float panelBottom = panelY + panelH * 0.5f;

    // ===== 布局常量（大字号两行式行高） =====
    const float rowH = 72.f;
    const float viewTop    = panelTop + 52.f;
    const float viewBottom = panelBottom - 26.f;
    const float viewH      = viewBottom - viewTop;
    const float contentH   = (float)defs.size() * rowH;
    const float maxScroll  = std::max(0.f, contentH - viewH);
    const bool  scrollable = maxScroll > 0.5f;

    // 滑块几何
    const float trackX   = panelRight - 18.f;
    const float trackTop = viewTop + 2.f;
    const float trackH   = viewH - 4.f;
    const float thumbH   = scrollable ? std::min(trackH, std::max(36.f, viewH * viewH / contentH)) : 0.f;
    const float thumbRange = std::max(1.f, trackH - thumbH);

    ImGuiIO& io = ImGui::GetIO();

    // 滑块顶部（拖拽/翻页/绘制共用）；不可滚动时退化为轨道顶，避免除零
    const float thumbTop = scrollable ? trackTop + (achScroll / maxScroll) * thumbRange : trackTop;
    const bool  dragging = scrollThumb->IsCaptured();

    // ===== 滚动交互登记：热区几何与绘制同源，命中判定与派发归 UISystem =====
    // 悬停面板任意位置即可滚轮滚动（一步约两行）
    listZone->SetRect(UIRect::FromBounds(panelLeft, panelTop, panelRight, panelBottom));
    listZone->SetEnabled(scrollable);
    listZone->SetOnWheel([this, rowH](const UIEvent& e) {
        achScrollTarget -= e.wheel * rowH * 2.f;
    });

    // 滑轨空白区：点击向点击方向翻一页
    scrollTrack->SetRect(UIRect::FromBounds(trackX - 11.f, trackTop,
                                           trackX + 11.f, trackTop + trackH));
    scrollTrack->SetEnabled(scrollable);
    scrollTrack->SetOnClick([this, thumbTop, viewH]() {
        achScrollTarget += (UISystem::Get().GetPointer().y < thumbTop) ? -viewH : viewH;
    });

    // 滑块：命中区略大于绘制宽度便于抓取；按下即捕获，拖拽直接定位（不做平滑）
    scrollThumb->SetRect(UIRect::FromBounds(trackX - 9.f, thumbTop - 5.f,
                                           trackX + 9.f, thumbTop + thumbH + 5.f));
    scrollThumb->SetEnabled(scrollable);
    scrollThumb->SetOnPress([this, thumbTop](const UIEvent& e) {
        achScrollbarGrabOffset = e.downPointer.y - thumbTop;
    });
    scrollThumb->SetOnDrag([this, trackTop, thumbRange, maxScroll](const UIEvent& e) {
        const float newTop = e.pointer.y - achScrollbarGrabOffset;
        achScroll = achScrollTarget = (newTop - trackTop) / thumbRange * maxScroll;
    });

    // 收敛 + 平滑趋近
    achScrollTarget = std::clamp(achScrollTarget, 0.f, maxScroll);
    if (scrollable && !dragging) {
        const float t = 1.f - std::exp(-14.f * io.DeltaTime);
        achScroll += (achScrollTarget - achScroll) * t;
        if (std::fabs(achScrollTarget - achScroll) < 0.5f) achScroll = achScrollTarget;
    }
    achScroll = std::clamp(achScroll, 0.f, maxScroll);

    // ===== 头部：成就等级计数 =====
    UIBuilder::DrawCenteredText(
        "成就等级  " + std::to_string(sys.TotalLevel()) + " / " +
            std::to_string((int)defs.size() * Achievements::MaxLevel) + " 已达成",
        panelX, panelTop + 24.f, 24.f, UITheme::ToImU32(UITheme::Amber));

    // ===== 行：名称（大字）+ 描述（次行），右侧圆形等级记号；视口裁剪 =====
    dl->PushClipRect(ImVec2(panelLeft, viewTop), ImVec2(panelRight, viewBottom), true);
    for (size_t i = 0; i < defs.size(); ++i) {
        const auto& d = defs[i];
        const float y = viewTop + rowH * 0.5f + (float)i * rowH - achScroll;
        if (y < viewTop - rowH || y > viewBottom + rowH) continue; // 视口外整行剔除

        const int lv = sys.Level(d.id); // 当前达成等级（0~MaxLevel）
        const bool on = lv >= 1;

        // 名称（左对齐，大字：达成金色 / 未达成月光白半透明）
        ImU32 nameCol = on ? UITheme::ToImU32(UITheme::Amber)
                           : UITheme::ToImU32(UITheme::Moonlight, 0.45f);
        dl->AddText(font, 26.f, ImVec2(panelLeft + 24.f + 2, y - 26.f + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), d.name);
        dl->AddText(font, 26.f, ImVec2(panelLeft + 24.f, y - 26.f), nameCol, d.name);

        // 描述（第二行）
        dl->AddText(font, 18.f, ImVec2(panelLeft + 24.f + 2, y + 6.f + 2),
                    ImGui::GetColorU32(ImVec4(0, 0, 0, 0.7f)), d.desc);
        dl->AddText(font, 18.f, ImVec2(panelLeft + 24.f, y + 6.f),
                    UITheme::ToImU32(UITheme::Moonlight, on ? 0.65f : 0.45f), d.desc);

        // 圆形等级记号（右侧三个圆点：实心金=已达成该等级，空心=未达成）
        const float dotCx = panelRight - 70.f;
        const float dotY = y - 8.f;
        const float dotR = 7.f;
        const float dotGap = 24.f;
        for (int t = 0; t < Achievements::MaxLevel; ++t) {
            ImVec2 c(dotCx + (t - 1) * dotGap, dotY);
            if (t < lv) {
                dl->AddCircleFilled(c, dotR, UITheme::ToImU32(UITheme::Amber));
                dl->AddCircle(c, dotR, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), 24, 1.5f);
            } else {
                dl->AddCircle(c, dotR, UITheme::ToImU32(UITheme::Moonlight, 0.35f), 24, 1.5f);
            }
        }

        // 行分隔线
        if (i + 1 < defs.size()) {
            float lineY = y + rowH * 0.5f;
            dl->AddLine(ImVec2(panelLeft + 20.f, lineY), ImVec2(panelRight - 20.f, lineY),
                        UITheme::ToImU32(UITheme::AmberDim, 0.5f));
        }
    }
    dl->PopClipRect();

    if (!scrollable) return;

    // ===== 上下边缘渐隐（与榜单一致） =====
    const float fadeH = 18.f;
    dl->AddRectFilledMultiColor(
        ImVec2(panelLeft, viewTop), ImVec2(panelRight, viewTop + fadeH),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f), UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),  UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f));
    dl->AddRectFilledMultiColor(
        ImVec2(panelLeft, viewBottom - fadeH), ImVec2(panelRight, viewBottom),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),  UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.0f),
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f), UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.85f));

    // ===== 右侧滑块（金色，hover 提亮、拖拽最亮） =====
    const float curTop = thumbTop;
    const bool thumbHot = dragging || scrollThumb->IsHovered();

    dl->AddRectFilled(ImVec2(trackX - 3.f, trackTop), ImVec2(trackX + 3.f, trackTop + trackH),
                      UITheme::ToImU32(UITheme::AmberDim, 0.30f), 3.0f);
    const ImU32 thumbCol = dragging         ? UITheme::ToImU32({1.0f, 0.87f, 0.52f})
                          : thumbHot            ? UITheme::ToImU32(UITheme::Amber)
                                                : UITheme::ToImU32(UITheme::Amber, 0.85f);
    dl->AddRectFilled(ImVec2(trackX - 4.5f, curTop), ImVec2(trackX + 4.5f, curTop + thumbH),
                      thumbCol, 4.5f);

    // ===== 底部提示 =====
    UIBuilder::DrawCenteredText("滚轮或拖动滑块查看全部成就",
                                panelX, panelBottom - 13.f, 18.f,
                                UITheme::ToImU32(UITheme::Moonlight, 0.60f));
}
