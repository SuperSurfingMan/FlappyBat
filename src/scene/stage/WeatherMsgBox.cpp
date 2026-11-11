#include "scene/stage/WeatherMsgBox.h"

#include "ui/UIBuilder.h"
#include "ui/UITheme.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

// 消息盒顶 y：贴着 SCORE 栏下缘（+2px 间距），与 StageHud 的 SCORE 底板几何一致
float WeatherBoxTopY() {
    const ImGuiStyle& st = ImGui::GetStyle();
    constexpr float kScoreH = 56.0f;
    const float fpsBottom = 10.0f + st.WindowPadding.y * 2.0f + ImGui::GetTextLineHeight();
    return fpsBottom + 2.0f + kScoreH + 2.0f;
}

constexpr float kBoxX = 24.0f;
constexpr float kBoxW = 224.0f;
constexpr float kHeaderH = 32.0f;
constexpr float kLineH = 30.0f;

} // namespace

void WeatherMsgBox::Reset() {
    msgs_.clear();
    clock_ = 0.0f;
}

void WeatherMsgBox::Post(int slot, const std::string& text, float flashTime, bool autoSteady) {
    // 已存在同 id：刷新内容并重新置顶；否则插入到首位（最新消息排第一行）
    for (auto it = msgs_.begin(); it != msgs_.end(); ++it) {
        if (it->id == slot) {
            Msg m = *it;
            m.text = text;
            m.warnTimer = flashTime;
            m.steady = false;
            m.autoSteady = autoSteady;
            msgs_.erase(it);
            msgs_.insert(msgs_.begin(), m);
            return;
        }
    }
    Msg m;
    m.id = slot;
    m.text = text;
    m.warnTimer = flashTime;
    m.autoSteady = autoSteady;
    msgs_.insert(msgs_.begin(), m);
    if (msgs_.size() > MsgCap) msgs_.resize(MsgCap);
}

void WeatherMsgBox::SetSteady(int slot, const std::string& text) {
    for (auto& m : msgs_) {
        if (m.id == slot) {
            m.text = text;
            m.steady = true;
            m.warnTimer = 0.0f;
            return;
        }
    }
    // 不存在（跳过了预警阶段）：直接以常驻状态行置顶加入
    Msg m;
    m.id = slot;
    m.text = text;
    m.steady = true;
    msgs_.insert(msgs_.begin(), m);
}

void WeatherMsgBox::Remove(int slot) {
    for (auto it = msgs_.begin(); it != msgs_.end(); ++it) {
        if (it->id == slot) {
            msgs_.erase(it); // vector 抹除后，后续消息自动上移补位
            return;
        }
    }
}

bool WeatherMsgBox::Exists(int slot, bool* steady) const {
    for (const auto& m : msgs_) {
        if (m.id == slot) {
            if (steady) *steady = m.steady;
            return true;
        }
    }
    return false;
}

void WeatherMsgBox::Tick(float dt) {
    clock_ += dt;

    // 预警闪烁计时：autoSteady 条目到点自转常驻；其余保持闪烁直到所属系统显式转换
    for (auto& m : msgs_) {
        if (!m.steady && m.warnTimer > 0.0f) {
            m.warnTimer -= dt;
            if (m.warnTimer <= 0.0f) {
                if (m.autoSteady) m.steady = true;
                else m.warnTimer = 0.0f;
            }
        }
    }
}

void WeatherMsgBox::Draw() const {
    if (msgs_.empty()) return;
    ImFont* font = UIBuilder::GetFont();
    if (!font) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // 左上角盒：贴着 SCORE 栏下缘（高度可变，向下延伸）
    const float x0 = kBoxX;
    const float top = WeatherBoxTopY();
    const float w = kBoxW;
    const float headerH = kHeaderH;
    const float lineH = kLineH;
    const float h = headerH + lineH * (float)msgs_.size() + 8.0f;

    // 底板整体提高透明度（文字与状态圆点除外），减少对游戏背景的遮挡
    UIBuilder::DrawRoundedRect(x0 + w * 0.5f, top + h * 0.5f, w, h, 14.0f,
        UITheme::ToImU32({0.07f, 0.05f, 0.13f}, 0.45f),
        UITheme::ToImU32(UITheme::AmberDim, 0.55f), 2.0f);

    // 标题
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f + 1, top + 9.0f + 1),
                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), "天象预警");
    dl->AddText(font, 14.0f, ImVec2(x0 + 14.0f, top + 9.0f),
                UITheme::ToImU32(UITheme::AmberDim), "天象预警");

    // 消息行：最新在第一行；预警行闪烁（红点 + 琥珀文字脉冲），状态行常驻（金点 + 月光白）
    for (size_t i = 0; i < msgs_.size(); ++i) {
        const auto& m = msgs_[i];
        const float cy = top + headerH + lineH * (float)i + lineH * 0.5f;
        ImVec2 ts = UIBuilder::CalcTextSize(m.text, 18.0f);

        if (m.steady) {
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Amber), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Moonlight, 0.92f), m.text.c_str());
        } else {
            float pulse = 0.5f + 0.5f * std::sin(clock_ * 10.0f); // 0~1 闪烁脉冲
            float a = 0.35f + 0.65f * pulse;
            dl->AddCircleFilled(ImVec2(x0 + 22.0f, cy), 4.5f,
                                UITheme::ToImU32(UITheme::Blood, 0.4f + 0.6f * pulse), 10);
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f + 2, cy - ts.y * 0.5f + 2),
                        ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * a)), m.text.c_str());
            dl->AddText(font, 18.0f, ImVec2(x0 + 36.0f, cy - ts.y * 0.5f),
                        UITheme::ToImU32(UITheme::Amber, a), m.text.c_str());
        }
    }
}
