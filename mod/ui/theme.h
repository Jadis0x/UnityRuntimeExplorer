// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>

namespace ModUI::Theme {

    struct Palette {
        ImVec4 bg_base{ 0.055f, 0.055f, 0.055f, 0.98f };
        ImVec4 bg_overlay{ 0.075f, 0.075f, 0.075f, 0.98f };
        ImVec4 bg_elevated{ 0.095f, 0.095f, 0.095f, 0.98f };

        ImVec4 surface{ 0.105f, 0.105f, 0.105f, 0.96f };
        ImVec4 surface_raised{ 0.135f, 0.135f, 0.135f, 0.98f };
        ImVec4 surface_hover{ 0.180f, 0.180f, 0.180f, 0.98f };
        ImVec4 surface_active{ 0.240f, 0.240f, 0.240f, 1.00f };
        ImVec4 surface_glass{ 0.025f, 0.025f, 0.025f, 0.84f };

        ImVec4 border_subtle{ 0.800f, 0.800f, 0.800f, 0.18f };
        ImVec4 border_strong{ 0.850f, 0.850f, 0.850f, 0.36f };
        ImVec4 border_focus{ 0.950f, 0.950f, 0.950f, 0.78f };

        ImVec4 text_primary{ 0.940f, 0.940f, 0.940f, 1.00f };
        ImVec4 text_secondary{ 0.720f, 0.720f, 0.720f, 1.00f };
        ImVec4 text_muted{ 0.500f, 0.500f, 0.500f, 1.00f };
        ImVec4 text_disabled{ 0.390f, 0.390f, 0.390f, 0.72f };

        ImVec4 accent_a{ 0.900f, 0.900f, 0.900f, 1.00f };
        ImVec4 accent_b{ 0.720f, 0.720f, 0.720f, 1.00f };
        ImVec4 accent_c{ 0.980f, 0.980f, 0.980f, 1.00f };
        ImVec4 accent_warm{ 0.820f, 0.820f, 0.820f, 1.00f };
        ImVec4 accent_soft{ 0.900f, 0.900f, 0.900f, 0.14f };
        ImVec4 accent_line{ 0.900f, 0.900f, 0.900f, 0.62f };
        ImVec4 toggle_on{ 0.780f, 0.780f, 0.780f, 1.00f };

        ImVec4 success{ 0.760f, 0.760f, 0.760f, 1.00f };
        ImVec4 warning{ 0.700f, 0.700f, 0.700f, 1.00f };
        ImVec4 danger{ 0.840f, 0.840f, 0.840f, 1.00f };
        ImVec4 info{ 0.820f, 0.820f, 0.820f, 1.00f };

        ImVec4 shadow{ 0.00f, 0.00f, 0.00f, 0.32f };
    };

    struct Radius {
        float sm = 3.0f;
        float md = 4.0f;
        float lg = 5.0f;
        float xl = 6.0f;
        float pill = 999.0f;
    };

    struct Spacing {
        ImVec2 window{ 16.0f, 14.0f };
        ImVec2 frame{ 10.0f, 6.0f };
        ImVec2 item{ 8.0f, 7.0f };
        ImVec2 card{ 18.0f, 16.0f };
        ImVec2 content{ 22.0f, 20.0f };
        float header_height = 82.0f;
        float footer_height = 0.0f;
        float sidebar_width = 0.0f;
        float section_gap = 14.0f;
        float hero_height = 64.0f;
        float widget_height = 36.0f;
        float label_column_width = 124.0f;
        float tab_gap = 4.0f;
    };

    inline Palette& palette() {
        static Palette value{};
        return value;
    }

    inline Radius& radius() {
        static Radius value{};
        return value;
    }

    inline Spacing& spacing() {
        static Spacing value{};
        return value;
    }

    inline ImVec4 with_alpha(ImVec4 c, float alpha) {
        c.w = alpha;
        return c;
    }

    inline ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
    }

    inline const ImWchar* glyph_ranges() {
        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1
            0x0100, 0x017F, // Latin Extended-A, including Turkish glyphs
            0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
            0x2000, 0x206F, // General punctuation
            0x20A0, 0x20CF, // Currency symbols
            0,
        };
        return ranges;
    }

    inline float dpi_scale();
    inline const char*& loaded_font_name() {
        static const char* value = "default";
        return value;
    }

    inline bool& using_ttf_font() {
        static bool value = false;
        return value;
    }

    inline const char*& cjk_font_support() {
        static const char* value = "unavailable";
        return value;
    }

    inline ImFont*& heading_font() {
        static ImFont* value = nullptr;
        return value;
    }

    inline ImFont* load_windows_font(ImGuiIO& io, const char* file_name, float size_px,
        const ImWchar* ranges = glyph_ranges(), bool merge_mode = false) {
        char windows_dir[MAX_PATH]{};
        const UINT windows_dir_len = GetWindowsDirectoryA(windows_dir, MAX_PATH);
        if (!io.Fonts || windows_dir_len == 0 || windows_dir_len >= MAX_PATH) return nullptr;

        char font_path[MAX_PATH]{};
        const int written = std::snprintf(font_path, sizeof(font_path), "%s\\Fonts\\%s", windows_dir, file_name);
        if (written <= 0 || written >= static_cast<int>(sizeof(font_path))) return nullptr;

        ImFontConfig config{};
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.RasterizerMultiply = 1.0f;
        config.GlyphOffset = ImVec2(0.0f, 0.0f);
        config.MergeMode = merge_mode;
        std::snprintf(config.Name, IM_ARRAYSIZE(config.Name), "%s %.1fpx", file_name, size_px);
        return io.Fonts->AddFontFromFileTTF(font_path, size_px, &config, ranges);
    }

    inline bool merge_first_windows_font(ImGuiIO& io, const char* const* file_names, size_t file_name_count,
        float size_px, const ImWchar* ranges) {
        for (size_t index = 0; index < file_name_count; ++index) {
            if (load_windows_font(io, file_names[index], size_px, ranges, true)) return true;
        }
        return false;
    }

    inline void merge_cjk_fallbacks(ImGuiIO& io, float size_px) {
        static const char* const japanese_fonts[] = { "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc" };
        static const char* const chinese_fonts[] = { "msyh.ttc", "simsun.ttc", "msjh.ttc" };
        const bool japanese_loaded = merge_first_windows_font(io, japanese_fonts, IM_ARRAYSIZE(japanese_fonts),
            size_px, io.Fonts->GetGlyphRangesJapanese());
        const bool chinese_loaded = merge_first_windows_font(io, chinese_fonts, IM_ARRAYSIZE(chinese_fonts),
            size_px, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        cjk_font_support() = japanese_loaded && chinese_loaded ? "Japanese and Chinese"
            : japanese_loaded ? "Japanese only" : chinese_loaded ? "Chinese only" : "unavailable";
    }

    inline float font_size_px() {
        float size = std::floor((18.0f * dpi_scale()) + 0.5f);
        if (size < 18.0f) size = 18.0f;
        if (size > 28.0f) size = 28.0f;
        return size;
    }

    inline void install_default_font() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts && io.Fonts->Fonts.Size > 0) {
            if (!io.FontDefault) io.FontDefault = io.Fonts->Fonts[0];
            heading_font() = io.FontDefault;
            return;
        }
        if (io.Fonts) io.Fonts->TexGlyphPadding = 2;
        loaded_font_name() = "default";
        using_ttf_font() = false;
        const float size_px = font_size_px();
        ImFont* regular = load_windows_font(io, "segoeui.ttf", size_px);
        if (regular) {
            merge_cjk_fallbacks(io, size_px);
            io.FontDefault = regular;
            heading_font() = load_windows_font(io, "seguisb.ttf", size_px + 2.0f);
            if (heading_font()) merge_cjk_fallbacks(io, size_px + 2.0f);
            else heading_font() = regular;
            loaded_font_name() = "segoeui.ttf";
            using_ttf_font() = true;
        }
        else {
            ImFontConfig config{};
            config.SizePixels = size_px;
            config.OversampleH = 2;
            config.OversampleV = 1;
            config.PixelSnapH = true;
            config.RasterizerMultiply = 1.0f;
            std::snprintf(config.Name, IM_ARRAYSIZE(config.Name), "ImGui default %.1fpx", size_px);
            io.FontDefault = io.Fonts->AddFontDefault(&config);
            heading_font() = io.FontDefault;
        }
        io.FontGlobalScale = 1.0f;
    }

    inline float dpi_scale() {
        HWND hwnd = GetActiveWindow();
        if (!hwnd) hwnd = GetForegroundWindow();

        UINT dpi = 96;
        if (hwnd) {
            dpi = GetDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
        }

        float scale = static_cast<float>(dpi) / 96.0f;
        if (scale < 1.0f) scale = 1.0f;
        if (scale > 2.0f) scale = 2.0f;
        return scale;
    }

    inline float pulse(float speed = 1.0f, float min_value = 0.0f, float max_value = 1.0f) {
        const double wave = (std::sin(ImGui::GetTime() * speed) + 1.0) * 0.5;
        return min_value + static_cast<float>(wave) * (max_value - min_value);
    }

    inline void gradient_rect(ImDrawList* dl, const ImVec2& p_min, const ImVec2& p_max,
        ImU32 col_left, ImU32 col_right, float rounding = 0.0f) {
        if (rounding <= 0.0f) {
            dl->AddRectFilledMultiColor(p_min, p_max, col_left, col_right, col_right, col_left);
            return;
        }
        const float width = p_max.x - p_min.x;
        const float cap = rounding;
        dl->AddRectFilled(p_min, p_max, col_left, rounding);
        if (width <= cap * 2.0f) return;
        dl->AddRectFilled(ImVec2(p_max.x - cap * 2.0f, p_min.y), p_max, col_right, rounding,
            ImDrawFlags_RoundCornersRight);
        dl->AddRectFilledMultiColor(ImVec2(p_min.x + cap, p_min.y), ImVec2(p_max.x - cap, p_max.y),
            col_left, col_right, col_right, col_left);
    }

    inline ImU32 lerp_col(const ImVec4& a, const ImVec4& b, float t) {
        return ImGui::GetColorU32(mix(a, b, t));
    }

    inline void glow_circle(ImDrawList* dl, const ImVec2& center, float radius,
        const ImVec4& color, float alpha_scale = 1.0f) {
        for (int ring = 3; ring >= 1; --ring) {
            const float expand = radius * (0.26f * static_cast<float>(ring));
            const float alpha = (0.07f / static_cast<float>(ring)) * alpha_scale;
            dl->AddCircleFilled(center, radius + expand,
                ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha)), 32);
        }
        dl->AddCircleFilled(center, radius,
            ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f * alpha_scale)), 32);
    }

    inline void apply() {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigWindowsMoveFromTitleBarOnly = false;
        install_default_font();

        ImGuiStyle& style = ImGui::GetStyle();
        const Palette& p = palette();
        const Radius& r = radius();
        const Spacing& sp = spacing();

        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.5f;
        style.WindowPadding = sp.window;
        style.FramePadding = sp.frame;
        style.ItemSpacing = sp.item;
        style.ItemInnerSpacing = ImVec2(9.0f, 7.0f);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 8.0f;
        style.GrabMinSize = 12.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.AntiAliasedFill = true;
        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;

        style.WindowRounding = r.xl;
        style.ChildRounding = r.lg;
        style.FrameRounding = r.md;
        style.PopupRounding = r.md;
        style.GrabRounding = r.pill;
        style.ScrollbarRounding = r.pill;
        style.TabRounding = r.md;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = p.text_primary;
        colors[ImGuiCol_TextDisabled] = p.text_disabled;
        colors[ImGuiCol_WindowBg] = p.bg_base;
        colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg] = p.bg_elevated;
        colors[ImGuiCol_Border] = p.border_subtle;
        colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

        colors[ImGuiCol_FrameBg] = p.surface;
        colors[ImGuiCol_FrameBgHovered] = p.surface_hover;
        colors[ImGuiCol_FrameBgActive] = p.surface_active;

        colors[ImGuiCol_TitleBg] = p.bg_elevated;
        colors[ImGuiCol_TitleBgActive] = p.bg_elevated;
        colors[ImGuiCol_TitleBgCollapsed] = p.bg_base;
        colors[ImGuiCol_MenuBarBg] = p.bg_elevated;

        colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ScrollbarGrab] = p.surface_hover;
        colors[ImGuiCol_ScrollbarGrabHovered] = p.surface_active;
        colors[ImGuiCol_ScrollbarGrabActive] = p.accent_a;

        colors[ImGuiCol_CheckMark] = p.accent_a;
        colors[ImGuiCol_SliderGrab] = p.accent_a;
        colors[ImGuiCol_SliderGrabActive] = p.accent_b;

        colors[ImGuiCol_Button] = p.surface;
        colors[ImGuiCol_ButtonHovered] = p.surface_hover;
        colors[ImGuiCol_ButtonActive] = p.surface_active;

        colors[ImGuiCol_Header] = with_alpha(p.accent_a, 0.14f);
        colors[ImGuiCol_HeaderHovered] = with_alpha(p.accent_a, 0.22f);
        colors[ImGuiCol_HeaderActive] = with_alpha(p.accent_b, 0.30f);

        colors[ImGuiCol_Separator] = p.border_subtle;
        colors[ImGuiCol_SeparatorHovered] = p.border_strong;
        colors[ImGuiCol_SeparatorActive] = p.accent_a;

        colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ResizeGripHovered] = p.accent_soft;
        colors[ImGuiCol_ResizeGripActive] = p.accent_a;

        colors[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_TabHovered] = p.surface_hover;
        colors[ImGuiCol_TabActive] = p.surface_active;

        colors[ImGuiCol_NavHighlight] = p.accent_a;
        colors[ImGuiCol_PlotLines] = p.accent_b;
        colors[ImGuiCol_PlotHistogram] = p.accent_c;
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    }
} // namespace ModUI::Theme
