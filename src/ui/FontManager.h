#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "imgui.h"

class FontManager {
public:
    enum class FontType {
        VonwaonBitmap16
    };

    static FontManager& Instance() {
        static FontManager instance;
        return instance;
    }

    ImFont* GetFont(FontType type) const {
        auto it = ImFontPtrs.find(type);
        if (it != ImFontPtrs.end()) {
            return it->second;
        }
        return nullptr;
    }

    void InitFont() {
        ImGuiIO& io = ImGui::GetIO();
        ImFont* vonwaonFont = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/VonwaonBitmap-16px.ttf",
            16.0f,
            NULL,
            io.Fonts->GetGlyphRangesChineseFull()
        );
        ImFontPtrs[FontType::VonwaonBitmap16] = vonwaonFont;
    }

private:
    FontManager() = default;
    ~FontManager() = default;

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    std::unordered_map<FontType, ImFont*> ImFontPtrs;
};