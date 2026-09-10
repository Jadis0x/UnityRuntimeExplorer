// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// The Unity 6 dark editor skin, reproduced for the Explorer overlay: the same
// flat greys, square corners, 1px near-black rules and #2C5D87 selection the
// editor uses, plus the handful of chrome widgets (toolbar strip, component
// header band, label/field property rows) that give Unity its layout rhythm.

#include <imgui.h>

namespace Explorer::UI::Unity {

// Sampled from the Unity 6 dark editor chrome. Names describe the role in the
// editor, not the colour, so a panel asks for `Skin::field` rather than a hex.
namespace Skin {
inline constexpr ImVec4 window{0.2196f, 0.2196f, 0.2196f, 1.0f};        // #383838 panel body
inline constexpr ImVec4 toolbar{0.2353f, 0.2353f, 0.2353f, 1.0f};       // #3C3C3C toolbar / headers
inline constexpr ImVec4 tab_bar{0.1725f, 0.1725f, 0.1725f, 1.0f};       // #2C2C2C strip behind tabs
inline constexpr ImVec4 popup{0.2431f, 0.2431f, 0.2431f, 1.0f};         // #3E3E3E menus
inline constexpr ImVec4 rule{0.1373f, 0.1373f, 0.1373f, 1.0f};          // #232323 1px separators
inline constexpr ImVec4 field{0.1647f, 0.1647f, 0.1647f, 1.0f};         // #2A2A2A input well
inline constexpr ImVec4 field_hover{0.1961f, 0.1961f, 0.1961f, 1.0f};   // #323232
inline constexpr ImVec4 button{0.3451f, 0.3451f, 0.3451f, 1.0f};        // #585858
inline constexpr ImVec4 button_hover{0.4039f, 0.4039f, 0.4039f, 1.0f};  // #676767
inline constexpr ImVec4 button_active{0.2784f, 0.2784f, 0.2784f, 1.0f}; // #474747
inline constexpr ImVec4 selection{0.1725f, 0.3647f, 0.5294f, 1.0f};     // #2C5D87 focused row
inline constexpr ImVec4 selection_hover{0.2039f, 0.4275f, 0.6157f, 1.0f};
inline constexpr ImVec4 selection_idle{0.3020f, 0.3020f, 0.3020f, 1.0f}; // #4D4D4D unfocused row
inline constexpr ImVec4 header{0.2549f, 0.2549f, 0.2549f, 1.0f};        // #414141 component band
inline constexpr ImVec4 header_hover{0.2902f, 0.2902f, 0.2902f, 1.0f};  // #4A4A4A
inline constexpr ImVec4 text{0.8118f, 0.8118f, 0.8118f, 1.0f};          // #CFCFCF
inline constexpr ImVec4 text_dim{0.4980f, 0.4980f, 0.4980f, 1.0f};      // #7F7F7F
inline constexpr ImVec4 accent{0.4039f, 0.5882f, 0.8471f, 1.0f};        // #6796D8 prefab / link blue
inline constexpr ImVec4 warning{0.9020f, 0.7255f, 0.3255f, 1.0f};       // #E6B953 console warning
inline constexpr ImVec4 error{0.8471f, 0.3922f, 0.3529f, 1.0f};         // #D8645A console error
inline constexpr ImVec4 scroll_grab{0.3529f, 0.3529f, 0.3529f, 1.0f};   // #5A5A5A
inline constexpr ImVec4 scroll_grab_hover{0.4471f, 0.4471f, 0.4471f, 1.0f};

// Toolbar accents. The editor's chrome is deliberately grey, but its toolbar is
// the one strip that colours what it does - the Play button lights up, the
// cloud and account controls carry their own tint - because a row of identical
// grey slabs tells the eye nothing about which of them it wants. These stay
// desaturated enough to sit inside the dark skin.
inline constexpr ImVec4 action_blue{0.2392f, 0.4314f, 0.6196f, 1.0f};  // #3D6E9E refresh
inline constexpr ImVec4 action_green{0.2745f, 0.4784f, 0.3059f, 1.0f}; // #467A4E live data
inline constexpr ImVec4 action_amber{0.5412f, 0.4157f, 0.1804f, 1.0f}; // #8A6A2E camera
inline constexpr ImVec4 action_violet{0.4392f, 0.3529f, 0.6118f, 1.0f}; // #705A9C pick
} // namespace Skin

// Style vars `push_style` pushes, so callers can pop the exact count.
inline constexpr int kStyleVarCount = 20;

// Unity's editor chrome is an 11pt UI. The overlay's shared font is sized for a
// mod menu, so the Explorer pushes its own size instead - and, since what counts
// as readable depends on the screen, that size is scalable. 1.0 is Unity's own
// size at 100% display scaling.
float &ui_scale();
float font_size();

// Editor metrics, in unscaled pixels at the compact font size.
float row_height();
float toolbar_height();

// Pushes the skin's colours and metrics. Returns the colour count to hand back
// to `pop_style`; the style var count is always `kStyleVarCount`.
// `opacity` fades only the large surfaces so the overlay stays readable above a
// bright scene - text, controls and rules stay opaque.
int push_style(float opacity);
void pop_style(int pushed_colors);

// A 1px #232323 rule across the content width, the way Unity separates its
// toolbar, headers and inspector sections.
void rule_line(float top_padding = 0.0f, float bottom_padding = 0.0f);

// Toolbar controls: square, full toolbar height. The accent overloads colour
// the control by what it does; hover and pressed states are derived from that
// colour, so a caller never has to supply three of them.
// Width of the button toolbar_button()/toolbar_toggle() would draw for `label`,
// so a row can be measured against the space left before it is drawn.
float toolbar_control_width(const char *label);
bool toolbar_button(const char *label, const char *tooltip = nullptr);
bool toolbar_button(const char *label, const ImVec4 &accent, const char *tooltip = nullptr);
// A toggle is `accent` while on and neutral grey while off, so its state reads
// at a glance instead of needing to be compared with its neighbours.
bool toolbar_toggle(const char *label, bool active, const char *tooltip = nullptr);
bool toolbar_toggle(const char *label, bool active, const ImVec4 &accent, const char *tooltip = nullptr);
void toolbar_separator();

// Unity's component band: full-width strip, disclosure triangle, optional
// enable checkbox and a dimmed right-aligned suffix. `enabled` may be null for
// components that cannot be toggled. Returns the open state; the header is the
// last item, so `BeginPopupContextItem` still attaches to it.
bool component_header(const char *str_id, const char *label, bool *enabled, bool *enabled_changed,
                      const char *right_text = nullptr);

// Fields, Properties and Methods otherwise read as one undifferentiated strip
// of grey tabs. Each kind carries its own tint so the eye can jump to the one
// it wants. Used exactly like BeginTabItem - it pops its own colours.
bool begin_member_tab(const char *label, const ImVec4 &accent, ImGuiTabItemFlags flags = 0);

// Label column / field column rows, at Unity's ~38% label split.
bool begin_property_rows(const char *str_id);
void end_property_rows();
void property_row(const char *label, const char *tooltip = nullptr);

// Unity's X/Y/Z triple: three drag fields, each with its own axis prefix.
bool vector3_row(const char *str_id, float value[3], float speed, const char *format);

} // namespace Explorer::UI::Unity
