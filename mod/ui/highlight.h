// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sdk/unity/unity.h"

namespace ModUI::Highlight {
using Unity::Camera;
using Unity::Component;
using Unity::DiagnosticSink;
using Unity::GameObject;
using Unity::ProjectionResult;
using Unity::Transform;
using Unity::Vector2;
using Unity::Vector3;

using HighlightId = std::uint32_t;
using ProjectWorldInfoFn = bool (*)(const Vector3& world, ProjectionResult* projection, void* user);

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
    // Reprojects entries only after an explicit dirty/remove request.
    EventDriven,
};

struct UpdatePolicy {
    UpdateMode mode = UpdateMode::Budgeted;
    // Zero disables the per-frame update limit.
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

struct Entry {
    HighlightId id = 0;
    TargetKind kind = TargetKind::None;
    Transform transform{};
    Vector3 world{};
    ImVec2 screen_min{};
    ImVec2 screen_max{};
    Style style{};
    std::string label{};
    bool enabled = true;
    std::uint8_t failed_frames = 0;
    std::uint32_t last_validation_frame = 0;
    std::uint32_t last_debug_frame = 0;
    DebugState last_debug_state = DebugState::None;
    ProjectionResult last_projection{};
    std::uint32_t last_projection_frame = 0;
    bool projection_dirty = true;
    bool has_projection = false;
};

struct ResolvedDraw {
    bool rect = false;
    bool indicator = false;
    bool label = false;
    ImVec2 min{};
    ImVec2 max{};
    ImVec2 line_from{};
    ImVec2 line_to{};
    ImVec2 arrow_left{};
    ImVec2 arrow_right{};
    ImVec2 label_pos{};
    const char* label_text = nullptr;
    bool label_centered = true;
};

class Manager {
    struct FrameProjectionCache {
        bool default_camera_resolved = false;
        Camera default_camera{};
        bool snapshot_resolved = false;
        bool snapshot_valid = false;
        Vector2 screen_size{};
        Vector2 screen_center{};
        Vector3 camera_position{};
        Vector3 camera_forward{0.0f, 0.0f, 1.0f};
        Vector3 camera_right{1.0f, 0.0f, 0.0f};
        Vector3 camera_up{0.0f, 1.0f, 0.0f};
        bool have_camera_basis = false;
    };

    enum class PendingKind : std::uint8_t {
        AddTransform,
        AddWorldPoint,
        AddScreenRect,
        MarkDirty,
        MarkAllDirty,
        SetWorldPoint,
        SetScreenRect,
        Remove,
        Clear,
    };

    struct PendingCommand {
        PendingKind kind = PendingKind::MarkDirty;
        HighlightId id = 0;
        Transform transform{};
        Vector3 world{};
        ImVec2 screen_min{};
        ImVec2 screen_max{};
        Style style{};
        std::string label{};
    };

public:
    HighlightId add(GameObject object, Style style = {}) {
        return add(object, nullptr, style);
    }

    HighlightId add(GameObject object, const char* label, Style style = {}) {
        return static_cast<bool>(object) ? add(object.transform(), label, style) : 0;
    }

    HighlightId add(Component component, Style style = {}) {
        return add(component, nullptr, style);
    }

    HighlightId add(Component component, const char* label, Style style = {}) {
        return static_cast<bool>(component) ? add(component.transform(), label, style) : 0;
    }

    HighlightId add(Transform transform, Style style = {}) {
        return add(transform, nullptr, style);
    }

    HighlightId add(Transform transform, const char* label, Style style = {}) {
        if (!static_cast<bool>(transform)) {
            log_text("[highlight] add(transform) rejected: target transform is null");
            return 0;
        }
        return add_transform_entry(allocate_id(), transform, label, style);
    }

    HighlightId enqueue_add(Transform transform, Style style = {}) {
        return enqueue_add(transform, nullptr, style);
    }

    HighlightId enqueue_add(Transform transform, const char* label, Style style = {}) {
        if (!static_cast<bool>(transform)) {
            log_text("[highlight] enqueue_add(transform) rejected: target transform is null");
            return 0;
        }
        PendingCommand command{};
        command.kind = PendingKind::AddTransform;
        command.id = allocate_id();
        command.transform = transform;
        command.style = style;
        command.label = safe_label(label);
        const HighlightId id = command.id;
        enqueue(std::move(command));
        return id;
    }

    HighlightId enqueue_add(GameObject object, const char* label, Style style = {}) {
        return static_cast<bool>(object) ? enqueue_add(object.transform(), label, style) : 0;
    }

    HighlightId enqueue_add(GameObject object, Style style = {}) {
        return enqueue_add(object, nullptr, style);
    }

    HighlightId enqueue_add(Component component, const char* label, Style style = {}) {
        return static_cast<bool>(component) ? enqueue_add(component.transform(), label, style) : 0;
    }

    HighlightId enqueue_add(Component component, Style style = {}) {
        return enqueue_add(component, nullptr, style);
    }

private:
    HighlightId add_transform_entry(HighlightId id, Transform transform,
                                    const char* label, Style style) {
        Entry entry{};
        entry.id = id;
        entry.kind = TargetKind::Transform;
        entry.transform = transform;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "transform target registered");
        return entry.id;
    }

public:

    HighlightId add_world_point(Vector3 world, Style style = {}) {
        return add_world_point(world, nullptr, style);
    }

    HighlightId add_world_point(Vector3 world, const char* label, Style style = {}) {
        return add_world_point_entry(allocate_id(), world, label, style);
    }

    HighlightId enqueue_add_world_point(Vector3 world, Style style = {}) {
        return enqueue_add_world_point(world, nullptr, style);
    }

    HighlightId enqueue_add_world_point(Vector3 world, const char* label, Style style = {}) {
        PendingCommand command{};
        command.kind = PendingKind::AddWorldPoint;
        command.id = allocate_id();
        command.world = world;
        command.style = style;
        command.label = safe_label(label);
        const HighlightId id = command.id;
        enqueue(std::move(command));
        return id;
    }

private:
    HighlightId add_world_point_entry(HighlightId id, Vector3 world,
                                      const char* label, Style style) {
        Entry entry{};
        entry.id = id;
        entry.kind = TargetKind::WorldPoint;
        entry.world = world;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "world point registered");
        return entry.id;
    }

public:

    HighlightId add_screen_rect(ImVec2 min, ImVec2 max, Style style = {}) {
        return add_screen_rect(min, max, nullptr, style);
    }

    HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char* label, Style style = {}) {
        Entry entry{};
        entry.id = allocate_id();
        entry.kind = TargetKind::ScreenRect;
        entry.screen_min = min;
        entry.screen_max = max;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "screen rect registered");
        return entry.id;
    }

    HighlightId enqueue_add_screen_rect(ImVec2 min, ImVec2 max, const char* label,
                                         Style style = {}) {
        PendingCommand command{};
        command.kind = PendingKind::AddScreenRect;
        command.id = allocate_id();
        command.screen_min = min;
        command.screen_max = max;
        command.style = style;
        command.label = safe_label(label);
        const HighlightId id = command.id;
        enqueue(std::move(command));
        return id;
    }

    HighlightId add_screen_rect(Vector2 min, Vector2 max, Style style = {}) {
        return add_screen_rect(to_imgui(min), to_imgui(max), style);
    }

    HighlightId add_screen_rect(Vector2 min, Vector2 max, const char* label, Style style = {}) {
        return add_screen_rect(to_imgui(min), to_imgui(max), label, style);
    }

    bool remove(HighlightId id) {
        return remove_entry(id);
    }

    void enqueue_remove(HighlightId id) {
        PendingCommand command{};
        command.kind = PendingKind::Remove;
        command.id = id;
        enqueue(std::move(command));
    }

private:
    bool remove_entry(HighlightId id) {
        const auto found = index_.find(id);
        if (found == index_.end()) return false;
        Entry& entry = entries_[found->second];
        note_state(entry, DebugState::Removed, nullptr, "highlight removed");
        erase_at(found->second);
        return true;
    }

public:

    void clear() {
        entries_.clear();
        index_.clear();
        update_cursor_ = 0;
    }

    void enqueue_clear() {
        PendingCommand command{};
        command.kind = PendingKind::Clear;
        enqueue(std::move(command));
    }

    void set_enabled(HighlightId id, bool enabled) {
        if (Entry* entry = find(id)) {
            entry->enabled = enabled;
            if (enabled && entry->kind != TargetKind::ScreenRect)
                entry->projection_dirty = true;
        }
    }

    void set_style(HighlightId id, Style style) {
        if (Entry* entry = find(id)) {
            entry->style = style;
            entry->projection_dirty = true;
        }
    }

    void set_label(HighlightId id, const char* label) {
        if (Entry* entry = find(id)) entry->label = safe_label(label);
    }

    void set_world_point(HighlightId id, Vector3 world) {
        if (Entry* entry = find(id)) {
            entry->kind = TargetKind::WorldPoint;
            entry->world = world;
            entry->failed_frames = 0;
            entry->enabled = true;
            entry->projection_dirty = true;
        }
    }

    void mark_dirty(HighlightId id) {
        if (Entry* entry = find(id)) {
            entry->projection_dirty = true;
            entry->enabled = true;
        }
    }

    void mark_all_dirty() {
        for (Entry& entry : entries_) {
            if (entry.kind != TargetKind::ScreenRect)
                entry.projection_dirty = true;
        }
    }

    void enqueue_mark_dirty(HighlightId id) {
        PendingCommand command{};
        command.kind = PendingKind::MarkDirty;
        command.id = id;
        enqueue(std::move(command));
    }

    void enqueue_mark_all_dirty() {
        PendingCommand command{};
        command.kind = PendingKind::MarkAllDirty;
        enqueue(std::move(command));
    }

    void enqueue_set_world_point(HighlightId id, Vector3 world) {
        PendingCommand command{};
        command.kind = PendingKind::SetWorldPoint;
        command.id = id;
        command.world = world;
        enqueue(std::move(command));
    }

    void enqueue_set_screen_rect(HighlightId id, ImVec2 min, ImVec2 max) {
        PendingCommand command{};
        command.kind = PendingKind::SetScreenRect;
        command.id = id;
        command.screen_min = min;
        command.screen_max = max;
        enqueue(std::move(command));
    }

    void set_screen_rect(HighlightId id, ImVec2 min, ImVec2 max) {
        if (Entry* entry = find(id)) {
            entry->kind = TargetKind::ScreenRect;
            entry->screen_min = min;
            entry->screen_max = max;
            entry->failed_frames = 0;
            entry->enabled = true;
        }
    }

    void set_screen_rect(HighlightId id, Vector2 min, Vector2 max) {
        set_screen_rect(id, to_imgui(min), to_imgui(max));
    }

    void set_screen_center(HighlightId id, ImVec2 center) {
        if (Entry* entry = find(id)) {
            const float half_w = entry->style.width * 0.5f;
            const float half_h = entry->style.height * 0.5f;
            set_screen_rect(id,
                ImVec2(center.x - half_w, center.y - half_h),
                ImVec2(center.x + half_w, center.y + half_h));
        }
    }

    void set_screen_center(HighlightId id, Vector2 center) {
        set_screen_center(id, to_imgui(center));
    }

    void set_projector_info(ProjectWorldInfoFn projector, void* user = nullptr) {
        projector_info_ = projector;
        projector_user_ = user;
        mark_all_dirty();
        log_text(projector
            ? "[highlight] projection-info callback registered"
            : "[highlight] projection-info callback cleared; main-camera projection helper will be used");
    }

    void set_diagnostics(DiagnosticSink sink) {
        diagnostics_ = sink;
    }

    void set_verbose_diagnostics(bool enabled) {
        verbose_diagnostics_ = enabled;
    }

    void set_diagnostic_throttle_frames(std::uint32_t frames) {
        diagnostic_throttle_frames_ = frames ? frames : 1;
    }

    void set_update_policy(UpdatePolicy policy) {
        policy.projection_interval_frames =
            std::max<std::uint32_t>(1, policy.projection_interval_frames);
        policy.camera_resolve_interval_frames =
            std::max<std::uint32_t>(1, policy.camera_resolve_interval_frames);
        policy.transform_validation_interval_frames =
            std::max<std::uint32_t>(1, policy.transform_validation_interval_frames);
        update_policy_ = policy;
        mark_all_dirty();
    }

    UpdatePolicy update_policy() const {
        return update_policy_;
    }

    FrameStats last_frame_stats() const {
        return frame_stats_;
    }

    void render(ImDrawList* draw_list = nullptr) {
        if (!draw_list) draw_list = ImGui::GetBackgroundDrawList();
        if (!draw_list) return;
        apply_pending();
        ++frame_index_;
        frame_stats_ = {};
        frame_stats_.targets = entries_.size();
        FrameProjectionCache frame{};
        update_projection_cache(frame);
        for (std::size_t i = 0; i < entries_.size();) {
            Entry& entry = entries_[i];
            if (!entry.enabled) {
                ++i;
                continue;
            }
            ResolvedDraw draw{};
            const ResolveState state = resolve_draw(entry, draw);
            if (state == ResolveState::Failed) {
                if (!entry.enabled && entry.kind == TargetKind::Transform) {
                    erase_at(i);
                    continue;
                }
                ++i;
                continue;
            }
            if (state == ResolveState::Skipped) {
                ++i;
                continue;
            }
            if (draw.rect)
                draw_rect(draw_list, draw.min, draw.max, entry.style);
            if (draw.indicator)
                draw_indicator(draw_list, draw, entry.style);
            if (draw.label)
                draw_label(draw_list, draw, entry.style);
            if (entry.kind != TargetKind::ScreenRect &&
                entry.last_projection_frame != frame_index_)
                ++frame_stats_.cached_projection_draws;
            ++i;
        }
    }

    std::size_t size() const {
        return entries_.size();
    }

private:
    HighlightId allocate_id() {
        HighlightId id = next_id_.fetch_add(1, std::memory_order_relaxed);
        if (id == 0)
            id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    void enqueue(PendingCommand command) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.push_back(std::move(command));
    }

    void apply_pending() {
        std::vector<PendingCommand> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending.swap(pending_);
        }
        for (PendingCommand& command : pending) {
            switch (command.kind) {
            case PendingKind::AddTransform:
                add_transform_entry(command.id, command.transform,
                    command.label.c_str(), command.style);
                break;
            case PendingKind::AddWorldPoint:
                add_world_point_entry(command.id, command.world,
                    command.label.c_str(), command.style);
                break;
            case PendingKind::AddScreenRect: {
                Entry entry{};
                entry.id = command.id;
                entry.kind = TargetKind::ScreenRect;
                entry.screen_min = command.screen_min;
                entry.screen_max = command.screen_max;
                entry.style = command.style;
                entry.label = std::move(command.label);
                index_[entry.id] = entries_.size();
                entries_.push_back(std::move(entry));
                break;
            }
            case PendingKind::MarkDirty:
                mark_dirty(command.id);
                break;
            case PendingKind::MarkAllDirty:
                mark_all_dirty();
                break;
            case PendingKind::SetWorldPoint:
                set_world_point(command.id, command.world);
                break;
            case PendingKind::SetScreenRect:
                set_screen_rect(command.id, command.screen_min, command.screen_max);
                break;
            case PendingKind::Remove:
                remove_entry(command.id);
                break;
            case PendingKind::Clear:
                clear();
                break;
            }
        }
    }

    Entry* find(HighlightId id) {
        const auto found = index_.find(id);
        return found != index_.end() ? &entries_[found->second] : nullptr;
    }

    ResolveState resolve_draw(Entry& entry, ResolvedDraw& draw) {
        if (entry.kind == TargetKind::ScreenRect) {
            draw.min = entry.screen_min;
            draw.max = entry.screen_max;
            if (!valid_rect(draw.min, draw.max)) {
                note_state(entry, DebugState::InvalidRect, nullptr, "screen rect is invalid");
                fail(entry, false);
                return ResolveState::Failed;
            }
            draw.rect = entry.style.draw_box;
            if (entry.style.draw_label && !entry.label.empty()) {
                const ImVec2 anchor((draw.min.x + draw.max.x) * 0.5f,
                                    entry.style.label_above_box ? draw.min.y : draw.max.y);
                configure_label(draw, entry, anchor);
            }
            note_state(entry, draw.rect ? DebugState::Rect : DebugState::Label);
            reset_failure(entry);
            return (draw.rect || draw.label) ? ResolveState::Drawn : ResolveState::Skipped;
        }

        if (!entry.has_projection)
            return ResolveState::Skipped;

        const ProjectionResult& projection = entry.last_projection;

        if (projection.on_screen &&
            entry.style.hide_within_distance > 0.0f &&
            projection.distance > 0.0f &&
            projection.distance <= entry.style.hide_within_distance) {
            note_state(entry, DebugState::TooClose, &projection,
                "target is inside the configured near-distance hide threshold");
            return ResolveState::Skipped;
        }

        if (projection.on_screen) {
            const float scale = screen_scale(entry.style, projection);
            const float half_w = (entry.style.width * scale) * 0.5f;
            const float half_h = (entry.style.height * scale) * 0.5f;
            draw.rect = entry.style.draw_box;
            draw.min = ImVec2(projection.screen.x - half_w, projection.screen.y - half_h);
            draw.max = ImVec2(projection.screen.x + half_w, projection.screen.y + half_h);
            if (draw.rect && !valid_rect(draw.min, draw.max)) {
                note_state(entry, DebugState::InvalidRect, &projection, "projected rect is invalid");
                fail(entry, false);
                return ResolveState::Failed;
            }
            if (entry.style.draw_label && !entry.label.empty()) {
                const ImVec2 anchor(projection.screen.x,
                    entry.style.label_above_box ? draw.min.y : draw.max.y);
                configure_label(draw, entry, anchor);
            }
            note_state(entry, draw.rect ? DebugState::Rect : DebugState::Label, &projection);
            return (draw.rect || draw.label) ? ResolveState::Drawn : ResolveState::Skipped;
        }

        if (entry.style.offscreen_indicator &&
            (projection.in_front || entry.style.draw_behind_indicator)) {
            const Vector2 center = projection.screen_center;
            Vector2 edge = projection.clamped_screen;
            Vector2 start = center + projection.direction * entry.style.indicator_center_gap;
            Vector2 end = edge;
            const float edge_distance = Vector2::distance(start, end);
            if (entry.style.indicator_length > 0.0f &&
                edge_distance > entry.style.indicator_length) {
                end = start + projection.direction * entry.style.indicator_length;
            }
            draw.indicator = true;
            draw.line_from = to_imgui(start);
            draw.line_to = to_imgui(end);
            const Vector2 arrow_base = end - projection.direction * entry.style.indicator_head_size;
            const Vector2 normal{-projection.direction.y, projection.direction.x};
            draw.arrow_left = to_imgui(
                arrow_base + normal * (entry.style.indicator_head_size * 0.62f));
            draw.arrow_right = to_imgui(
                arrow_base - normal * (entry.style.indicator_head_size * 0.62f));
            if (entry.style.draw_label && entry.style.label_show_offscreen && !entry.label.empty()) {
                configure_label(draw, entry,
                    to_imgui(end + projection.direction * (entry.style.label_offset + 6.0f)));
            }
            note_state(entry, DebugState::Indicator, &projection,
                projection.in_front ? "center-anchored offscreen arrow drawn"
                                    : "behind-camera arrow drawn from screen center");
            return ResolveState::Drawn;
        }

        note_state(entry, DebugState::Offscreen, &projection,
            projection.in_front ? "target is outside the current camera frustum"
                                : "target is behind the current camera");
        return ResolveState::Skipped;
    }

    void update_projection_cache(FrameProjectionCache& frame) {
        const std::size_t count = entries_.size();
        if (count == 0)
            return;

        const bool unlimited = update_policy_.mode == UpdateMode::EveryFrame ||
            update_policy_.max_updates_per_frame == 0;
        const std::size_t budget = unlimited
            ? count
            : std::min(update_policy_.max_updates_per_frame, count);
        std::size_t visited = 0;
        std::size_t updated = 0;
        update_cursor_ %= count;

        while (visited < count && updated < budget) {
            const std::size_t index = (update_cursor_ + visited) % count;
            Entry& entry = entries_[index];
            ++visited;
            if (!should_update_projection(entry))
                continue;

            ++updated;
            ++frame_stats_.projection_updates;
            if (!refresh_projection(entry, frame))
                ++frame_stats_.projection_failures;
        }

        update_cursor_ = (update_cursor_ + visited) % count;
    }

    bool should_update_projection(const Entry& entry) const {
        if (!entry.enabled || entry.kind == TargetKind::ScreenRect)
            return false;
        if (!entry.has_projection || entry.projection_dirty)
            return true;
        if (update_policy_.mode == UpdateMode::EventDriven)
            return false;
        if (update_policy_.mode == UpdateMode::EveryFrame)
            return true;
        return frame_index_ - entry.last_projection_frame >=
            update_policy_.projection_interval_frames;
    }

    bool refresh_projection(Entry& entry, FrameProjectionCache& frame) {
        ProjectionResult projection{};
        Vector3 world = entry.world;
        if (entry.kind == TargetKind::Transform) {
            if (!static_cast<bool>(entry.transform)) {
                note_state(entry, DebugState::MissingTransform, nullptr, "target transform handle is null");
                entry.has_projection = false;
                return fail(entry, true);
            }
            if (frame_index_ - entry.last_validation_frame >=
                update_policy_.transform_validation_interval_frames) {
                entry.last_validation_frame = frame_index_;
                if (!entry.transform.alive()) {
                    entry.enabled = false;
                    entry.has_projection = false;
                    note_state(entry, DebugState::DeadTransform, nullptr, "target transform is no longer alive");
                    return false;
                }
            }
            world = entry.transform.position();
            entry.world = world;
        }

        if (projector_info_) {
            if (!projector_info_(world, &projection, projector_user_)) {
                note_state(entry, DebugState::ProjectionFailed, nullptr, "projection-info callback returned false");
                entry.has_projection = false;
                return fail(entry, false);
            }
            projection.world = world;
            if (projection.screen_center.nearly_zero())
                projection.screen_center = fallback_screen_center();
            if (projection.direction.nearly_zero()) {
                Vector2 fallback_direction = projection.clamped_screen - projection.screen_center;
                if (fallback_direction.nearly_zero())
                    fallback_direction = projection.screen - projection.screen_center;
                projection.direction = fallback_direction.nearly_zero()
                    ? Vector2{0.0f, -1.0f}
                    : fallback_direction.normalized();
            }
            projection.valid = true;
        } else {
            projection = project_world_cached(frame, world, entry.style.indicator_padding);
            if (!projection.valid) {
                entry.has_projection = false;
                note_state(entry, DebugState::NoProjection, nullptr,
                    "default main-camera projection helper returned no result");
                return fail(entry, false);
            }
        }

        entry.last_projection = projection;
        entry.last_projection_frame = frame_index_;
        entry.projection_dirty = false;
        entry.has_projection = true;
        reset_failure(entry);
        return true;
    }

    ProjectionResult project_world_cached(FrameProjectionCache& frame,
                                          Vector3 world,
                                          float edge_padding) {
        ProjectionResult result{};
        result.world = world;
        if (!resolve_camera_snapshot(frame))
            return result;

        result.screen_center = frame.screen_center;
        const Vector3 offset = world - frame.camera_position;
        result.distance = offset.magnitude();
        if (frame.have_camera_basis && result.distance > 0.000001f)
            result.facing = Vector3::dot(offset / result.distance, frame.camera_forward);

        result.screen3 = frame.default_camera.WorldToScreenPoint(world);
        result.depth = result.screen3.z;
        result.in_front = result.depth > 0.01f;
        result.screen = {
            result.screen3.x,
            frame.screen_size.y - result.screen3.y
        };

        if (update_policy_.use_viewport_projection) {
            result.viewport = frame.default_camera.WorldToViewportPoint(world);
            result.on_screen = result.in_front &&
                result.viewport.x >= 0.0f && result.viewport.x <= 1.0f &&
                result.viewport.y >= 0.0f && result.viewport.y <= 1.0f;
        } else {
            result.viewport = {
                frame.screen_size.x > 0.0f ? result.screen3.x / frame.screen_size.x : 0.0f,
                frame.screen_size.y > 0.0f ? result.screen3.y / frame.screen_size.y : 0.0f,
                result.depth
            };
            result.on_screen = result.in_front &&
                result.screen3.x >= 0.0f && result.screen3.x <= frame.screen_size.x &&
                result.screen3.y >= 0.0f && result.screen3.y <= frame.screen_size.y;
        }

        Vector2 direction = result.screen - result.screen_center;
        if (!result.on_screen && frame.have_camera_basis && result.distance > 0.000001f) {
            const Vector3 offset_direction = offset / result.distance;
            direction = {
                Vector3::dot(offset_direction, frame.camera_right),
                -Vector3::dot(offset_direction, frame.camera_up)
            };
            if (!result.in_front)
                direction *= -1.0f;
        } else if (!result.in_front) {
            direction *= -1.0f;
        }
        if (direction.nearly_zero())
            direction = {0.0f, -1.0f};

        result.direction = direction.normalized();
        result.clamped_screen = direction_to_screen_edge(
            result.direction, edge_padding, frame.screen_size);
        result.valid = true;
        return result;
    }

    bool resolve_camera_snapshot(FrameProjectionCache& frame) {
        if (frame.snapshot_resolved)
            return frame.snapshot_valid;
        frame.snapshot_resolved = true;
        frame.default_camera = default_camera(frame);
        if (!static_cast<bool>(frame.default_camera))
            return false;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        frame.screen_size = {
            display.x > 0.0f ? display.x : 0.0f,
            display.y > 0.0f ? display.y : 0.0f
        };
        if (frame.screen_size.x <= 0.0f || frame.screen_size.y <= 0.0f) {
            const int width = frame.default_camera.pixelWidth();
            const int height = frame.default_camera.pixelHeight();
            frame.screen_size = {
                static_cast<float>(width > 0 ? width : 0),
                static_cast<float>(height > 0 ? height : 0)
            };
        }
        if (frame.screen_size.x <= 0.0f || frame.screen_size.y <= 0.0f)
            return false;
        frame.screen_center = frame.screen_size * 0.5f;

        const Transform camera_transform = frame.default_camera.transform();
        if (static_cast<bool>(camera_transform)) {
            frame.camera_position = camera_transform.position();
            frame.camera_forward = camera_transform.forward().normalized();
            frame.camera_right = camera_transform.right().normalized();
            frame.camera_up = camera_transform.up().normalized();
            frame.have_camera_basis = true;
        }
        frame.snapshot_valid = true;
        return true;
    }

    static Vector2 direction_to_screen_edge(Vector2 direction,
                                            float padding,
                                            Vector2 screen_size) {
        const Vector2 center = screen_size * 0.5f;
        Vector2 normalized = direction.normalized();
        if (normalized.nearly_zero())
            normalized = {0.0f, -1.0f};
        const float half_width = std::max(1.0f, screen_size.x * 0.5f - padding);
        const float half_height = std::max(1.0f, screen_size.y * 0.5f - padding);
        float x_scale = 1000000.0f;
        float y_scale = 1000000.0f;
        if (std::fabs(normalized.x) > 0.000001f)
            x_scale = half_width / std::fabs(normalized.x);
        if (std::fabs(normalized.y) > 0.000001f)
            y_scale = half_height / std::fabs(normalized.y);
        return center + normalized * std::min(x_scale, y_scale);
    }

    bool fail(Entry& entry, bool persistent) {
        if (persistent) {
            if (entry.failed_frames < 255) ++entry.failed_frames;
            if (entry.failed_frames >= 16)
                entry.enabled = false;
        }
        return false;
    }

    void reset_failure(Entry& entry) {
        entry.failed_frames = 0;
    }

    void erase_at(std::size_t idx) {
        if (idx >= entries_.size()) return;
        const HighlightId removed = entries_[idx].id;
        const std::size_t last = entries_.size() - 1;
        if (idx != last) {
            entries_[idx] = entries_[last];
            index_[entries_[idx].id] = idx;
        }
        entries_.pop_back();
        index_.erase(removed);
    }

    static ImVec2 to_imgui(Vector2 value) {
        return ImVec2(value.x, value.y);
    }

    static Vector2 fallback_screen_center() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x > 0.0f && display.y > 0.0f)
            return {display.x * 0.5f, display.y * 0.5f};
        return Unity::Screen::center();
    }

    Camera default_camera(FrameProjectionCache& frame) {
        if (!frame.default_camera_resolved) {
            const bool refresh = !static_cast<bool>(cached_camera_) ||
                frame_index_ - last_camera_resolve_frame_ >=
                    update_policy_.camera_resolve_interval_frames;
            if (refresh) {
                cached_camera_ = Camera::main();
                last_camera_resolve_frame_ = frame_index_;
            }
            frame.default_camera = cached_camera_;
            frame.default_camera_resolved = true;
        }
        return frame.default_camera;
    }

    static bool valid_rect(ImVec2 min, ImVec2 max) {
        return max.x > min.x && max.y > min.y &&
               min.x > -100000.0f && min.y > -100000.0f &&
               max.x < 100000.0f && max.y < 100000.0f;
    }

    static void draw_rect(ImDrawList* draw_list, ImVec2 min, ImVec2 max, const Style& style) {
        if (style.shadow) {
            const ImVec2 shadow(style.shadow_offset, style.shadow_offset);
            const ImVec2 shadow_min(min.x + shadow.x, min.y + shadow.y);
            const ImVec2 shadow_max(max.x + shadow.x, max.y + shadow.y);
            if (style.filled) {
                draw_list->AddRectFilled(shadow_min, shadow_max, style.shadow_color, style.rounding);
            }
            if (style.corner_box) {
                draw_corner_box(draw_list, shadow_min, shadow_max, style.shadow_color, style);
            } else {
                draw_list->AddRect(shadow_min, shadow_max, style.shadow_color, style.rounding, 0, style.thickness);
            }
        }
        if (style.filled) {
            draw_list->AddRectFilled(min, max, style.fill_color, style.rounding);
        }
        if (style.corner_box) {
            draw_corner_box(draw_list, min, max, style.color, style);
        } else {
            draw_list->AddRect(min, max, style.color, style.rounding, 0, style.thickness);
        }
    }

    static void draw_indicator(ImDrawList* draw_list, ImVec2 from, ImVec2 to, const Style& style) {
        draw_list->AddLine(from, to, style.indicator_color, style.indicator_thickness);
    }

    static void draw_indicator(ImDrawList* draw_list, const ResolvedDraw& draw, const Style& style) {
        if (style.indicator_center_dot_radius > 0.0f) {
            draw_list->AddCircleFilled(draw.line_from, style.indicator_center_dot_radius, style.indicator_color);
        }
        draw_indicator(draw_list, draw.line_from, draw.line_to, style);
        draw_list->AddTriangleFilled(draw.line_to, draw.arrow_left, draw.arrow_right, style.indicator_color);
    }

    static void draw_label(ImDrawList* draw_list, const ResolvedDraw& draw, const Style& style) {
        if (!draw.label || !draw.label_text || !draw.label_text[0]) return;
        const ImVec2 text_size = ImGui::CalcTextSize(draw.label_text);
        ImVec2 min = draw.label_pos;
        if (draw.label_centered)
            min.x -= text_size.x * 0.5f;
        const ImVec2 pad = style.label_padding;
        const ImVec2 bg_min(min.x - pad.x, min.y - pad.y);
        const ImVec2 bg_max(min.x + text_size.x + pad.x, min.y + text_size.y + pad.y);
        if ((style.label_bg_color >> IM_COL32_A_SHIFT) != 0)
            draw_list->AddRectFilled(bg_min, bg_max, style.label_bg_color, style.label_rounding);
        if ((style.label_border_color >> IM_COL32_A_SHIFT) != 0)
            draw_list->AddRect(bg_min, bg_max, style.label_border_color, style.label_rounding);
        draw_list->AddText(min, style.label_color, draw.label_text);
    }

    static const char* debug_state_name(DebugState state) {
        switch (state) {
        case DebugState::Added: return "added";
        case DebugState::Rect: return "rect";
        case DebugState::Label: return "label";
        case DebugState::Indicator: return "indicator";
        case DebugState::Offscreen: return "offscreen";
        case DebugState::TooClose: return "too_close";
        case DebugState::InvalidRect: return "invalid_rect";
        case DebugState::MissingTransform: return "missing_transform";
        case DebugState::DeadTransform: return "dead_transform";
        case DebugState::NoProjection: return "no_projection";
        case DebugState::ProjectionFailed: return "projection_failed";
        case DebugState::Removed: return "removed";
        default: return "none";
        }
    }

    void note_state(Entry& entry, DebugState state,
                    const ProjectionResult* projection = nullptr,
                    const char* detail = nullptr) {
        const bool emit = diagnostics_ &&
            (verbose_diagnostics_ ||
             state != entry.last_debug_state ||
             frame_index_ - entry.last_debug_frame >= diagnostic_throttle_frames_);
        entry.last_debug_state = state;
        entry.last_debug_frame = frame_index_;
        if (!emit)
            return;

        char buffer[768]{};
        if (projection) {
            std::snprintf(
                buffer, sizeof(buffer),
                "[highlight] id=%u state=%s enabled=%s detail=%s world=(%.2f, %.2f, %.2f) "
                "screen=(%.2f, %.2f) clamped=(%.2f, %.2f) depth=%.3f distance=%.3f facing=%.3f "
                "inFront=%s onScreen=%s",
                entry.id,
                debug_state_name(state),
                entry.enabled ? "true" : "false",
                detail ? detail : "",
                projection->world.x, projection->world.y, projection->world.z,
                projection->screen.x, projection->screen.y,
                projection->clamped_screen.x, projection->clamped_screen.y,
                projection->depth,
                projection->distance,
                projection->facing,
                projection->in_front ? "true" : "false",
                projection->on_screen ? "true" : "false");
        } else {
            std::snprintf(
                buffer, sizeof(buffer),
                "[highlight] id=%u state=%s enabled=%s detail=%s",
                entry.id,
                debug_state_name(state),
                entry.enabled ? "true" : "false",
                detail ? detail : "");
        }
        diagnostics_(buffer);
    }

    void log_text(const char* text) const {
        if (diagnostics_ && text && text[0])
            diagnostics_(text);
    }

    static std::string safe_label(const char* label) {
        return (label && label[0]) ? std::string(label) : std::string{};
    }

    static float screen_scale(const Style& style, const ProjectionResult& projection) {
        if (!style.scale_with_distance || projection.distance <= 0.001f)
            return 1.0f;
        const float reference = style.reference_distance > 0.001f ? style.reference_distance : 1.0f;
        const float raw = reference / projection.distance;
        const float min_scale = std::min(style.min_scale, style.max_scale);
        const float max_scale = std::max(style.min_scale, style.max_scale);
        return std::clamp(raw, min_scale, max_scale);
    }

    static void configure_label(ResolvedDraw& draw, const Entry& entry, ImVec2 anchor) {
        if (!entry.style.draw_label || entry.label.empty())
            return;
        draw.label = true;
        draw.label_text = entry.label.c_str();
        draw.label_centered = true;
        draw.label_pos = ImVec2(anchor.x,
            entry.style.label_above_box
                ? anchor.y - ImGui::GetTextLineHeight() - (entry.style.label_offset + entry.style.label_padding.y * 2.0f)
                : anchor.y + entry.style.label_offset);
    }

    static void draw_corner_box(ImDrawList* draw_list, ImVec2 min, ImVec2 max, ImU32 color, const Style& style) {
        const float len = std::max(2.0f, std::min(style.corner_length,
            std::min(max.x - min.x, max.y - min.y) * 0.5f));
        const float t = style.thickness;
        draw_list->AddLine(min, ImVec2(min.x + len, min.y), color, t);
        draw_list->AddLine(min, ImVec2(min.x, min.y + len), color, t);
        draw_list->AddLine(ImVec2(max.x - len, min.y), ImVec2(max.x, min.y), color, t);
        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, t);
        draw_list->AddLine(ImVec2(min.x, max.y - len), ImVec2(min.x, max.y), color, t);
        draw_list->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, t);
        draw_list->AddLine(ImVec2(max.x - len, max.y), max, color, t);
        draw_list->AddLine(ImVec2(max.x, max.y - len), max, color, t);
    }

    std::vector<Entry> entries_{};
    std::unordered_map<HighlightId, std::size_t> index_{};
    std::atomic<HighlightId> next_id_{1};
    std::mutex pending_mutex_{};
    std::vector<PendingCommand> pending_{};
    std::size_t update_cursor_ = 0;
    std::uint32_t frame_index_ = 0;
    std::uint32_t diagnostic_throttle_frames_ = 60;
    std::uint32_t last_camera_resolve_frame_ = 0;
    bool verbose_diagnostics_ = false;
    UpdatePolicy update_policy_{};
    FrameStats frame_stats_{};
    Camera cached_camera_{};
    ProjectWorldInfoFn projector_info_ = nullptr;
    void* projector_user_ = nullptr;
    DiagnosticSink diagnostics_ = nullptr;
};

inline Manager& manager() {
    static Manager instance;
    return instance;
}

inline HighlightId add(GameObject object, Style style = {}) {
    return manager().add(object, style);
}

inline HighlightId add(GameObject object, const char* label, Style style = {}) {
    return manager().add(object, label, style);
}

inline HighlightId add(Component component, Style style = {}) {
    return manager().add(component, style);
}

inline HighlightId add(Component component, const char* label, Style style = {}) {
    return manager().add(component, label, style);
}

inline HighlightId add(Transform transform, Style style = {}) {
    return manager().add(transform, style);
}

inline HighlightId add(Transform transform, const char* label, Style style = {}) {
    return manager().add(transform, label, style);
}

// Thread-safe variants for callers outside the render hook.
inline HighlightId enqueue_add(GameObject object, Style style = {}) {
    return manager().enqueue_add(object, style);
}

inline HighlightId enqueue_add(GameObject object, const char* label, Style style = {}) {
    return manager().enqueue_add(object, label, style);
}

inline HighlightId enqueue_add(Component component, Style style = {}) {
    return manager().enqueue_add(component, style);
}

inline HighlightId enqueue_add(Component component, const char* label, Style style = {}) {
    return manager().enqueue_add(component, label, style);
}

inline HighlightId enqueue_add(Transform transform, Style style = {}) {
    return manager().enqueue_add(transform, style);
}

inline HighlightId enqueue_add(Transform transform, const char* label, Style style = {}) {
    return manager().enqueue_add(transform, label, style);
}

inline HighlightId add_world_point(Vector3 world, Style style = {}) {
    return manager().add_world_point(world, style);
}

inline HighlightId add_world_point(Vector3 world, const char* label, Style style = {}) {
    return manager().add_world_point(world, label, style);
}

inline HighlightId enqueue_add_world_point(Vector3 world, Style style = {}) {
    return manager().enqueue_add_world_point(world, style);
}

inline HighlightId enqueue_add_world_point(Vector3 world, const char* label, Style style = {}) {
    return manager().enqueue_add_world_point(world, label, style);
}

inline HighlightId add_screen_rect(ImVec2 min, ImVec2 max, Style style = {}) {
    return manager().add_screen_rect(min, max, style);
}

inline HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char* label, Style style = {}) {
    return manager().add_screen_rect(min, max, label, style);
}

inline HighlightId enqueue_add_screen_rect(ImVec2 min, ImVec2 max, const char* label,
                                            Style style = {}) {
    // The render hook owns the immediate entry store. Queue the add so callers
    // from the update hook never race it.
    return manager().enqueue_add_screen_rect(min, max, label, style);
}

inline HighlightId add_screen_rect(Vector2 min, Vector2 max, Style style = {}) {
    return manager().add_screen_rect(min, max, style);
}

inline HighlightId add_screen_rect(Vector2 min, Vector2 max, const char* label, Style style = {}) {
    return manager().add_screen_rect(min, max, label, style);
}

inline bool remove(HighlightId id) {
    return manager().remove(id);
}

inline void clear() {
    manager().clear();
}

inline void set_label(HighlightId id, const char* label) {
    manager().set_label(id, label);
}

inline void mark_dirty(HighlightId id) {
    manager().mark_dirty(id);
}

inline void mark_all_dirty() {
    manager().mark_all_dirty();
}

inline void enqueue_mark_dirty(HighlightId id) {
    manager().enqueue_mark_dirty(id);
}

inline void enqueue_mark_all_dirty() {
    manager().enqueue_mark_all_dirty();
}

inline void enqueue_set_world_point(HighlightId id, Vector3 world) {
    manager().enqueue_set_world_point(id, world);
}

inline void enqueue_set_screen_rect(HighlightId id, ImVec2 min, ImVec2 max) {
    manager().enqueue_set_screen_rect(id, min, max);
}

inline void enqueue_remove(HighlightId id) {
    manager().enqueue_remove(id);
}

inline void enqueue_clear() {
    manager().enqueue_clear();
}

inline void set_update_policy(UpdatePolicy policy) {
    manager().set_update_policy(policy);
}

inline UpdatePolicy update_policy() {
    return manager().update_policy();
}

inline FrameStats last_frame_stats() {
    return manager().last_frame_stats();
}

inline void set_projector_info(ProjectWorldInfoFn projector, void* user = nullptr) {
    manager().set_projector_info(projector, user);
}

inline void set_diagnostics(DiagnosticSink sink) {
    manager().set_diagnostics(sink);
}

inline void set_verbose_diagnostics(bool enabled) {
    manager().set_verbose_diagnostics(enabled);
}

inline void set_diagnostic_throttle_frames(std::uint32_t frames) {
    manager().set_diagnostic_throttle_frames(frames);
}
} // namespace ModUI::Highlight
