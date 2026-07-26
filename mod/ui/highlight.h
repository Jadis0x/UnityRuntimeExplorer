// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity.h"

#include <cstddef>
#include <cstdint>
#include <imgui.h>

namespace ModUI::Highlight {

using Unity::Component;
using Unity::DiagnosticSink;
using Unity::GameObject;
using Unity::ProjectionResult;
using Unity::Transform;
using Unity::Vector2;
using Unity::Vector3;

using HighlightId = std::uint32_t;
using ProjectWorldInfoFn =
    bool (*)(const Vector3& world, ProjectionResult* projection, void* user);

enum class TargetKind : std::uint8_t {
    None,
    Transform,
    WorldPoint,
    ScreenRect,
};

enum class ResolveState : std::uint8_t {
    Drawn,
    Skipped,
    Failed,
};

enum class UpdateMode : std::uint8_t {
    EveryFrame,
    Budgeted,
    EventDriven,
};

struct UpdatePolicy {
    UpdateMode mode = UpdateMode::Budgeted;
    std::size_t max_updates_per_frame = 20;
    std::uint32_t projection_interval_frames = 2;
    std::uint32_t camera_resolve_interval_frames = 30;
    std::uint32_t transform_validation_interval_frames = 30;
    bool use_viewport_projection = false;
};

struct FrameStats {
    std::size_t targets = 0;
    std::size_t projection_updates = 0;
    std::size_t projection_failures = 0;
    std::size_t cached_projection_draws = 0;
};

enum class DebugState : std::uint8_t {
    None,
    Added,
    Rect,
    Label,
    Indicator,
    Offscreen,
    TooClose,
    TooFar,
    InvalidRect,
    MissingTransform,
    DeadTransform,
    NoProjection,
    ProjectionFailed,
    Removed,
};

struct Style {
    ImU32 color = IM_COL32(255, 213, 74, 235);
    ImU32 fill_color = IM_COL32(255, 213, 74, 26);
    ImU32 indicator_color = IM_COL32(255, 213, 74, 220);
    ImU32 label_color = IM_COL32(255, 255, 255, 235);
    ImU32 label_bg_color = IM_COL32(18, 18, 18, 205);
    ImU32 label_border_color = IM_COL32(255, 213, 74, 190);
    ImU32 shadow_color = IM_COL32(0, 0, 0, 120);
    float width = 92.0f;
    float height = 120.0f;
    float rounding = 2.0f;
    float thickness = 2.0f;
    float corner_length = 18.0f;
    float shadow_offset = 2.0f;
    float indicator_padding = 24.0f;
    float indicator_length = 84.0f;
    float indicator_thickness = 3.0f;
    float indicator_head_size = 12.0f;
    float indicator_center_gap = 16.0f;
    float indicator_center_dot_radius = 4.0f;
    float hide_within_distance = 0.0f;
    float max_distance = 0.0f;
    float reference_distance = 12.0f;
    float min_scale = 0.48f;
    float max_scale = 1.18f;
    float label_offset = 10.0f;
    float label_rounding = 4.0f;
    ImVec2 label_padding = ImVec2(8.0f, 4.0f);
    bool filled = false;
    bool draw_box = true;
    bool draw_label = false;
    bool label_above_box = false;
    bool label_show_offscreen = true;
    bool corner_box = false;
    bool shadow = false;
    bool scale_with_distance = true;
    bool offscreen_indicator = true;
    bool draw_behind_indicator = true;
};

HighlightId add(GameObject object, Style style = {});
HighlightId add(GameObject object, const char* label, Style style = {});
HighlightId add(Component component, Style style = {});
HighlightId add(Component component, const char* label, Style style = {});
HighlightId add(Transform transform, Style style = {});
HighlightId add(Transform transform, const char* label, Style style = {});

HighlightId enqueue_add(GameObject object, Style style = {});
HighlightId enqueue_add(GameObject object, const char* label, Style style = {});
HighlightId enqueue_add(Component component, Style style = {});
HighlightId enqueue_add(Component component, const char* label, Style style = {});
HighlightId enqueue_add(Transform transform, Style style = {});
HighlightId enqueue_add(Transform transform, const char* label, Style style = {});

HighlightId add_world_point(Vector3 world, Style style = {});
HighlightId add_world_point(Vector3 world, const char* label, Style style = {});
HighlightId enqueue_add_world_point(Vector3 world, Style style = {});
HighlightId enqueue_add_world_point(
    Vector3 world, const char* label, Style style = {});

HighlightId add_screen_rect(ImVec2 min, ImVec2 max, Style style = {});
HighlightId add_screen_rect(
    ImVec2 min, ImVec2 max, const char* label, Style style = {});
HighlightId enqueue_add_screen_rect(
    ImVec2 min, ImVec2 max, const char* label, Style style = {});
HighlightId add_screen_rect(Vector2 min, Vector2 max, Style style = {});
HighlightId add_screen_rect(
    Vector2 min, Vector2 max, const char* label, Style style = {});

bool remove(HighlightId id);
void clear();
void set_label(HighlightId id, const char* label);
void mark_dirty(HighlightId id);
void mark_all_dirty();
void enqueue_mark_dirty(HighlightId id);
void enqueue_mark_all_dirty();
void enqueue_set_label(HighlightId id, const char* label);
void enqueue_set_world_point(HighlightId id, Vector3 world);
void enqueue_set_screen_rect(HighlightId id, ImVec2 min, ImVec2 max);
void enqueue_remove(HighlightId id);
void enqueue_clear();
void set_update_policy(UpdatePolicy policy);
UpdatePolicy update_policy();
FrameStats last_frame_stats();
void set_projector_info(ProjectWorldInfoFn projector, void* user = nullptr);
void set_diagnostics(DiagnosticSink sink);
void set_verbose_diagnostics(bool enabled);
void set_diagnostic_throttle_frames(std::uint32_t frames);
void render();

} // namespace ModUI::Highlight
