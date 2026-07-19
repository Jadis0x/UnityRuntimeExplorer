// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "explorer/explorer_ui.h"
#include "theme.h"

namespace ModUI {

inline void initialize_style() {
  Theme::apply();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 3.0f;
  style.ChildRounding = 2.0f;
  style.FrameRounding = 2.0f;
  style.PopupRounding = 2.0f;
  style.TabRounding = 2.0f;
  style.WindowPadding = ImVec2(7.0f, 7.0f);
  style.ItemSpacing = ImVec2(6.0f, 5.0f);
}

inline void render_menu() {
  Explorer::UI::render();
}

}  // namespace ModUI
