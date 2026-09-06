// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "method_tracer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Explorer::UI {

// Bounded per-widget state. Evicting only the least recently touched entry
// keeps the editor the user is working in alive; a wholesale clear() would
// reset every remembered filter, draft and expansion at once the moment the
// cap was crossed.
template <class Key, class Value>
class UiStateCache {
  public:
    explicit UiStateCache(std::size_t capacity) : capacity_(capacity) {}

    template <class... Args>
    Value &touch(const Key &key, Args &&...args) {
        if (const auto found = entries_.find(key); found != entries_.end()) {
            order_.splice(order_.end(), order_, found->second.position);
            return found->second.value;
        }
        while (entries_.size() >= capacity_ && !order_.empty()) {
            entries_.erase(order_.front());
            order_.pop_front();
        }
        const auto position = order_.insert(order_.end(), key);
        return entries_.try_emplace(key, Entry{Value(std::forward<Args>(args)...), position}).first->second.value;
    }

    void clear() {
        entries_.clear();
        order_.clear();
    }

  private:
    struct Entry {
        Value value;
        typename std::list<Key>::iterator position;
    };
    std::size_t capacity_;
    std::list<Key> order_;
    std::unordered_map<Key, Entry> entries_;
};

// One member editor: the text being typed plus what the widget needs to know
// about the value behind it.
struct MemberBuffer {
    std::vector<char> text = std::vector<char>(256, '\0');
    bool active = false;
    bool dirty = false;
    bool pending = false;
    double pending_since = 0.0;
    bool bool_initialized = false;
    bool bool_value = false;
    bool structured_initialized = false;
    std::size_t component_count = 0;
    std::array<float, 8> components{};
    bool sample_requested = false;
};

// Per-trace view options in the trace viewer.
struct TraceViewState {
    std::array<char, 256> filter{};
    bool newest_first = true;
    bool show_raw_abi = false;
    bool show_addresses = false;
};

// Which member kind a member browser is showing.
enum MemberTab : int { MemberTabFields = 0, MemberTabProperties = 1, MemberTabMethods = 2 };

// Everything the panels remember between frames. Keeping it in one struct makes
// the lifetime explicit: selection changes and scene reloads clear exactly the
// caches they should, instead of each widget hiding a static of its own.
struct UiState {
    static constexpr std::size_t max_member_editors = 4096;

    // Member editing.
    UiStateCache<std::uint64_t, MemberBuffer> member_buffers{max_member_editors};
    UiStateCache<std::uint64_t, bool> method_boolean_arguments{max_member_editors};

    // Component inspector.
    UiStateCache<int, std::array<char, 128>> component_filters{1024};
    UiStateCache<int, bool> component_show_inherited{1024};
    UiStateCache<int, int> component_member_tabs{1024};

    // Object inspector.
    UiStateCache<std::uint64_t, std::array<char, 128>> object_member_filters{256};
    UiStateCache<std::uint64_t, int> object_member_tabs{256};

    // Trace and watch viewers.
    UiStateCache<MethodTracer::TraceId, TraceViewState> trace_views{64};
    UiStateCache<std::uint64_t, float> watch_threshold_drafts{64};

    // Dropped when the hierarchy selection changes, because every key in them is
    // scoped to the component that is going away.
    void clear_selection_scoped() {
        member_buffers.clear();
        method_boolean_arguments.clear();
        component_filters.clear();
        component_show_inherited.clear();
        component_member_tabs.clear();
    }
};

UiState &ui_state();

} // namespace Explorer::UI
