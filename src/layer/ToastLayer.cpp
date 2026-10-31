#include "layer/ToastLayer.h"
#include "ui/FontManager.h"
#include "render/Texture2D.h"
#include "imgui.h"
#include <cfloat>

ToastLayer::ToastLayer() {
    // 默认不参与更新/渲染；Activate 时整体打开
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

ToastLayer::ToastLayer(float duration, std::string text, Vec3 color, float textSize, Vec2 position) {
    this->duration = duration;
    this->text = text;
    this->color = color;
    this->textSize = textSize;
    this->position = position;

    SetUpdateEnabled(false);
    SetRenderEnabled(false);
    SetInputEnabled(false);
}

void ToastLayer::OnUpdate(float dt) {
    timer += dt;
    if (timer >= duration) {
        Reset();
        return;
    }
}

void ToastLayer::OnDraw(SpriteRenderer& renderer) {
    (void)renderer;
    DrawImGUILayout();
}

void ToastLayer::Reset() {
    timer = 0.f;
    alpha = 0.f;
    running = false;
    SetUpdateEnabled(false);
    SetRenderEnabled(false);
}

void ToastLayer::SetColor(Vec3 _color) {
    color = _color;
}

void ToastLayer::SetText(std::string _text) {
    text = _text;
}

void ToastLayer::SetBackground(std::shared_ptr<Texture2D> tex) {
    bgTex = tex;
}

bool ToastLayer::IsRunning() const {
    return running;
}

void ToastLayer::Activate() {
    Reset();
    running = true;
    SetUpdateEnabled(true);
    SetRenderEnabled(true);
}

void ToastLayer::DrawImGUILayout() {
    const float stage1Start = duration * 0.2;
    const float stage2Start = duration * (0.2 + 0.6);
    const float stage1or3Duration = stage1Start;
    if (timer < stage1Start) {
        float progress = 1.f - ((stage1Start - timer) / stage1or3Duration);
        DrawToastText(progress);
        return;
    }
    else if (timer < stage2Start) {
        DrawToastText(1.0f);
        return;
    }
    else {
        float progress = (timer - stage2Start) / stage1or3Duration;
        DrawToastText(1.f - progress);
        return;
    }
}

// 统一绘制：水平居中于 position.x，垂直以 position.y 为中心
void ToastLayer::DrawToastText(float alphaVal) {
    ImFont* chineseFont = FontManager::Instance().GetFont(FontManager::FontType::VonwaonBitmap16);
    if (!chineseFont) return;
    auto draw = ImGui::GetForegroundDrawList();

    // 可选背景纹理：以 position 为中心，随 alpha 同步淡入淡出（成就提示面板）
    // 注：纹理经 stb flipY 解码，GL 坐标 (0,0) 为图像底部；ImGui 屏幕 y 向下，
    // 故 uv 垂直翻转（左上取 (0,1)、右下取 (1,0)）才能正立显示。
    if (bgTex && bgTex->GetID() != 0) {
        const float bw = (float)bgTex->GetWidth();
        const float bh = (float)bgTex->GetHeight();
        draw->AddImage((ImTextureID)(ImU64)bgTex->GetID(),
                       ImVec2(position.x - bw * 0.5f, position.y - bh * 0.5f),
                       ImVec2(position.x + bw * 0.5f, position.y + bh * 0.5f),
                       ImVec2(0.f, 1.f), ImVec2(1.f, 0.f),
                       ImGui::GetColorU32(ImVec4(1, 1, 1, alphaVal)));
    }

    ImVec2 ts = chineseFont->CalcTextSizeA(textSize, FLT_MAX, -1.0f, text.c_str());
    ImVec2 pos(position.x - ts.x * 0.5f, position.y - ts.y * 0.5f);

    ImU32 shadow = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.8f * alphaVal));
    draw->AddText(chineseFont, textSize, ImVec2(pos.x + 2, pos.y + 2), shadow, text.c_str());

    ImU32 colorU32 = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alphaVal));
    draw->AddText(chineseFont, textSize, pos, colorU32, text.c_str());
}
