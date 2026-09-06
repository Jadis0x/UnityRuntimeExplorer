// Copyright (c) 2026 Jadis0x. All rights reserved.
// Method trace and value watch viewers.
#include "explorer_ui.h"

#include "config/mod_config.h"
#include "explorer_model.h"
#include "method_trace_format.h"
#include "ui_shared.h"
#include "ui_state.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::UI {
namespace {

std::string trace_seconds_json(double seconds) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.9f", seconds);
    return text;
}

void append_csv_value(std::string &out, std::string_view value) {
    out += '"';
    for (char character : value) {
        if (character == '"')
            out += '"';
        out += character;
    }
    out += '"';
}

TraceViewState &trace_view_state(MethodTracer::TraceId id) {
    return ui_state().trace_views.touch(id);
}

std::string short_trace_type_name(std::string_view type_name) {
    const std::size_t separator = type_name.rfind('.');
    return separator == std::string_view::npos ? std::string(type_name) : std::string(type_name.substr(separator + 1));
}

bool caller_is_unnamed(std::string_view caller) {
    return caller.empty() || caller.find(".dll+0x") != std::string_view::npos ||
           caller.find("<module>+0x") != std::string_view::npos;
}

std::string friendly_trace_caller(std::string_view caller) {
    if (caller.empty())
        return "caller address was not captured";
    // Keep the cell terse; the full explanation goes in a tooltip.
    if (caller.find("GameAssembly.dll+") != std::string_view::npos)
        return std::string(caller) + " (unnamed - build the caller index)";
    if (caller == "<shared managed generic code>")
        return std::string("shared generic ") + ModConfig::backend_name + " code";
    return std::string(caller);
}

void enqueue_method_trace_capture_returns(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::CaptureMethodTraceReturns;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_caller_index_build() {
    RuntimeModel::instance().enqueue(Command{.kind = CommandKind::BuildManagedCallerIndex});
}

// Naming a raw caller address needs the method index; offer building it here.
void render_caller_index_notice(const Snapshot &snapshot, const MethodTracer::Snapshot &trace) {
    const bool unnamed = std::any_of(trace.records.begin(), trace.records.end(),
                                     [](const MethodTracer::Record &record) {
                                         return caller_is_unnamed(record.caller_display);
                                     });
    if (snapshot.caller_index_active) {
        ImGui::TextColored(ImVec4(0.66f, 0.76f, 0.86f, 1.0f), "Indexing caller names... %zu method(s) in %zu class(es)",
                           snapshot.caller_index_methods, snapshot.caller_index_classes);
        return;
    }
    if (!unnamed)
        return;
    if (!snapshot.caller_index_supported) {
        ImGui::TextDisabled("Some callers are raw addresses. Naming them needs method address metadata this "
                            "runtime backend does not expose.");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.71f, 0.45f, 1.0f));
    ImGui::TextWrapped(snapshot.caller_index_built
                           ? "Some callers are still raw addresses: their code has no managed method behind it, "
                             "or it was compiled after the index was built."
                           : "Callers show as raw addresses because managed method addresses have not been indexed "
                             "yet.");
    ImGui::PopStyleColor();
    if (ImGui::SmallButton(snapshot.caller_index_built ? "Rebuild caller index" : "Build caller index"))
        enqueue_caller_index_build();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Walks every loaded assembly and records where each managed method starts.\n"
                          "Runs in slices in the background; existing trace rows pick the names up as it goes.");
    if (snapshot.caller_index_built) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu method(s) indexed", snapshot.caller_index_methods);
    }
}

// Explains why Returns is empty and offers the fix.
void render_return_capture_notice(const MethodTracer::Snapshot &trace) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.71f, 0.45f, 1.0f));
    ImGui::TextWrapped("Return values are not recorded. This trace hooks the method's entry, so it sees the "
                       "arguments and the caller but never the result.");
    ImGui::PopStyleColor();
    if (ImGui::SmallButton("Record return values"))
        enqueue_method_trace_capture_returns(trace.id);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-hooks the method so calls return through the tracer.\n"
                          "Clears the calls recorded so far, and is unsafe on methods that throw.");
}

// Hue derived from the value/type text so it stays stable across frames.
ImVec4 trace_value_color(std::string_view key) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char character : key) {
        hash ^= character;
        hash *= 16777619u;
    }
    const float hue = static_cast<float>(hash % 360u) / 360.0f;
    ImVec4 color{};
    ImGui::ColorConvertHSVtoRGB(hue, 0.46f, 0.98f, color.x, color.y, color.z);
    color.w = 1.0f;
    return color;
}

void trace_value_text(std::string_view key, std::string_view text, bool readable) {
    if (!readable) {
        ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, trace_value_color(key));
    ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
    ImGui::PopStyleColor();
}

// Renders a decoded value as a tree so nested fields/elements are reachable.
void render_trace_value_node(const MethodTracer::ValueNode &node, std::string_view fallback_name, int id);

void render_trace_value_children(const MethodTracer::ValueNode &node) {
    for (std::size_t index = 0; index < node.children.size(); ++index)
        render_trace_value_node(node.children[index], "value", static_cast<int>(index));
    if (node.truncated)
        ImGui::TextDisabled("... more members than the decoder walks in one pass");
}

void render_trace_value_node(const MethodTracer::ValueNode &node, std::string_view fallback_name, int id) {
    ImGui::PushID(id);
    std::string label(node.name.empty() ? fallback_name : std::string_view(node.name));
    if (!node.type.empty())
        label += "  (" + short_trace_type_name(node.type) + ")";
    label += "  =  " + (node.display.empty() ? std::string("<unavailable>") : node.display);

    const bool expandable = !node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!expandable)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    if (node.readable)
        ImGui::PushStyleColor(ImGuiCol_Text, trace_value_color(node.type + node.display));
    const bool open = ImGui::TreeNodeEx("##value", flags, "%s", label.c_str());
    if (node.readable)
        ImGui::PopStyleColor();
    if (node.inspect_address != 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Inspect"))
            enqueue_raw_reference_inspection(node.inspect_address);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Opens this reference in the Object Inspector.");
    }
    if (expandable && open) {
        render_trace_value_children(node);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void trace_card_row(std::string_view label, std::string_view color_key,
                    std::string_view value, bool readable, std::uint64_t inspect_address = 0) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%.*s", static_cast<int>(label.size()), label.data());
    ImGui::TableSetColumnIndex(1);
    trace_value_text(color_key, value, readable);
    const std::string copy_popup = "##copy-" + std::string(label);
    if (ImGui::BeginPopupContextItem(copy_popup.c_str())) {
        if (ImGui::MenuItem("Copy value"))
            ImGui::SetClipboardText(std::string(value).c_str());
        ImGui::EndPopup();
    }
    if (inspect_address != 0) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(inspect_address ^ (inspect_address >> 32)));
        if (ImGui::SmallButton("Inspect"))
            enqueue_raw_reference_inspection(inspect_address);
        ImGui::PopID();
    }
}

void render_method_trace(const Snapshot &snapshot, const MethodTracer::Snapshot &trace) {
    TraceViewState &state = trace_view_state(trace.id);
    if (trace.active) {
        ImGui::TextDisabled("%llu calls | %zu groups", static_cast<unsigned long long>(trace.total_calls),
                            trace.records.size());
    } else {
        ImGui::TextDisabled("Trace stopped: %llu calls recorded", static_cast<unsigned long long>(trace.total_calls));
    }
    if (trace.overwritten_records != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %llu older records overwritten", static_cast<unsigned long long>(trace.overwritten_records));
    }
    if (trace.native_faults != 0) {
        ImGui::TextColored(ImVec4(0.72f, 0.60f, 0.42f, 1.0f), "%llu capture faults were isolated",
                           static_cast<unsigned long long>(trace.native_faults));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        enqueue_method_trace_clear(trace.id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy CSV"))
        ImGui::SetClipboardText(MethodTraceFormat::csv(trace).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON"))
        ImGui::SetClipboardText(MethodTraceFormat::json(trace).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies schemaVersion 2 JSON with structured arguments and results.");
    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##trace-filter", "Filter caller, target or arguments...", state.filter.data(), state.filter.size());
    if (ImGui::SmallButton("Clear filter"))
        state.filter.fill('\0');
    ImGui::SameLine();
    ImGui::Checkbox("Newest first", &state.newest_first);
    ImGui::SameLine();
    ImGui::Checkbox("Show raw ABI", &state.show_raw_abi);
    ImGui::SameLine();
    ImGui::Checkbox("Addresses", &state.show_addresses);
    const std::string_view filter = state.filter.data();

    if (!trace.captures_return)
        render_return_capture_notice(trace);
    render_caller_index_notice(snapshot, trace);

    if (ImGui::CollapsingHeader("Technical details")) {
        ImGui::TextDisabled("Method metadata address: %s", trace.method_pointer_text.empty() ? "<unavailable>"
                                                                                               : trace.method_pointer_text.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy method address"))
            ImGui::SetClipboardText(trace.method_pointer_text.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the managed method metadata address.");
        ImGui::TextWrapped("Raw ABI preserves arguments plus RAX/XMM0 return lanes. Friendly values use runtime "
                           "type metadata and decode arbitrary value types through their fields on the Explorer thread. "
                           "Recent returned references are rooted for Object Inspector access.");
    }

    std::unordered_map<std::string, std::size_t> caller_counts;
    std::unordered_map<std::uint32_t, std::size_t> thread_counts;
    std::uint64_t collapsed_calls = 0;
    double latest_elapsed = 0.0;
    for (const MethodTracer::Record &record : trace.records) {
        const std::uint64_t repeat_count = std::max<std::uint64_t>(1, record.repeat_count);
        caller_counts[record.caller_display.empty() ? "<unresolved native caller>" : record.caller_display] +=
            static_cast<std::size_t>(repeat_count);
        thread_counts[record.thread_id] += static_cast<std::size_t>(repeat_count);
        collapsed_calls += repeat_count - 1;
        if (trace.timestamp_frequency && record.timestamp_ticks >= trace.start_timestamp_ticks)
            latest_elapsed = std::max(latest_elapsed, static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
                                                          static_cast<double>(trace.timestamp_frequency));
    }
    if (ImGui::CollapsingHeader("Trace statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Recorded calls: %llu  |  Shown groups: %zu  |  Rate: %.2f/s  |  Callers: %zu  |  Threads: %zu",
                    static_cast<unsigned long long>(trace.total_calls), trace.records.size(),
                    latest_elapsed > 0.0 ? trace.total_calls / latest_elapsed : 0.0, caller_counts.size(), thread_counts.size());
        if (collapsed_calls != 0)
            ImGui::TextDisabled("%llu consecutive duplicate calls collapsed; expand a group to see its range.",
                                static_cast<unsigned long long>(collapsed_calls));
        std::vector<std::pair<std::string, std::size_t>> callers(caller_counts.begin(), caller_counts.end());
        std::sort(callers.begin(), callers.end(), [](const auto &left, const auto &right) { return left.second > right.second; });
        const std::size_t shown = std::min<std::size_t>(callers.size(), 6);
        for (std::size_t index = 0; index < shown; ++index)
            ImGui::BulletText("%zu x %s", callers[index].second, callers[index].first.c_str());
        if (callers.size() > shown)
            ImGui::TextDisabled("%zu additional caller sites", callers.size() - shown);
    }
    if (trace.records.empty()) {
        ImGui::TextDisabled("Waiting for a call...");
        return;
    }
    ImGui::TextDisabled("CALLS  (right-click a value to copy)");
    ImGui::BeginChild("##method-trace-calls", ImVec2(0.0f, std::max(180.0f, ImGui::GetContentRegionAvail().y)),
                      true);
    {
        for (std::size_t displayed = 0; displayed < trace.records.size(); ++displayed) {
            const std::size_t record_index = state.newest_first ? trace.records.size() - 1 - displayed : displayed;
            const MethodTracer::Record &record = trace.records[record_index];
            const std::string argument_summary = MethodTraceFormat::argument_summary(trace, record);
            const std::string result = MethodTraceFormat::result(trace, record);
            if (!filter.empty() && !contains_case_insensitive(record.caller_display, filter) &&
                !contains_case_insensitive(record.target_display, filter) &&
                !contains_case_insensitive(argument_summary, filter) &&
                !contains_case_insensitive(result, filter))
                continue;
            const double elapsed = MethodTraceFormat::elapsed_seconds(trace, record);
            const std::string elapsed_text = MethodTraceFormat::elapsed_text(elapsed);
            const std::uint64_t repeat_count = std::max<std::uint64_t>(1, record.repeat_count);
            const std::uint64_t sequence_start = record.sequence_start == 0 ? record.sequence : record.sequence_start;
            std::string call_label = "#" + std::to_string(record.sequence);
            if (repeat_count > 1)
                call_label = "#" + std::to_string(sequence_start) + "-" + std::to_string(record.sequence) +
                    " (" + std::to_string(repeat_count) + ")";
            ImGui::PushID(static_cast<int>(record.sequence));
            const bool open = ImGui::TreeNodeEx(
                "##call", ImGuiTreeNodeFlags_SpanAvailWidth,
                "%s   +%s   thread %u", call_label.c_str(), elapsed_text.c_str(), record.thread_id);
            if (ImGui::BeginPopupContextItem("##copy-call-header")) {
                if (ImGui::MenuItem("Copy call summary"))
                    ImGui::SetClipboardText(
                        (call_label + "   +" + elapsed_text + "   thread " + std::to_string(record.thread_id)).c_str());
                ImGui::EndPopup();
            }

            const bool readable_arguments = std::any_of(
                record.argument_readable.begin(), record.argument_readable.end(),
                [](bool readable) { return readable; });
            const std::string caller = friendly_trace_caller(record.caller_display);
            if (ImGui::BeginTable("##trace-call-summary", 2,
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                const std::string when = "+" + elapsed_text + " | thread " + std::to_string(record.thread_id);
                const std::string what = trace.declaring_type + "." + trace.method_name;
                trace_card_row("When", when, when, false);
                if (repeat_count > 1) {
                    const std::string repeats = std::to_string(repeat_count) + " identical calls (" +
                        std::to_string(sequence_start) + "-" + std::to_string(record.sequence) + ")";
                    trace_card_row("Repeated", repeats, repeats, false);
                }
                trace_card_row("Who", caller, caller, false);
                trace_card_row("What", what, what, false);
                if (!trace.is_static)
                    trace_card_row("Target", record.target_display, record.target_display.empty()
                        ? std::string_view("<unavailable>") : std::string_view(record.target_display),
                        !record.target_display.empty(), record.target_address);
                trace_card_row("Arguments", argument_summary,
                               argument_summary.empty() ? std::string_view("none") : std::string_view(argument_summary),
                               readable_arguments);
                const std::string return_line =
                    trace.return_type.empty() || trace.return_type == "System.Void" || trace.return_type == "Void"
                        ? result
                        : result + "   (" + short_trace_type_name(trace.return_type) + ")";
                trace_card_row("Returns", trace.return_type + result, return_line, record.return_readable,
                               record.return_node.inspect_address);
                ImGui::EndTable();
            }

            if (open) {
                ImGui::Spacing();
                ImGui::SeparatorText("Call site");
                ImGui::TextWrapped("%s", caller.c_str());

                if (!trace.is_static) {
                    ImGui::SeparatorText("Target object");
                    trace_value_text(trace.declaring_type + record.target_display,
                                     record.target_display.empty() ? "<unavailable>" : record.target_display,
                                     !record.target_display.empty());
                    if (record.target_address != 0 && ImGui::SmallButton("Inspect target"))
                        enqueue_raw_reference_inspection(record.target_address);
                }

                ImGui::SeparatorText("Arguments");
                const std::vector<MethodTraceFormat::ArgumentView> argument_views =
                    MethodTraceFormat::arguments(trace, record);
                if (argument_views.empty()) {
                    ImGui::TextUnformatted("none");
                } else {
                    for (const MethodTraceFormat::ArgumentView& argument : argument_views) {
                        ImGui::PushID(static_cast<int>(argument.index));
                        ImGui::BulletText("%s", argument.name.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", argument.type.c_str());
                        ImGui::Indent();
                        const MethodTracer::ValueNode *node = argument.index < record.argument_nodes.size()
                            ? &record.argument_nodes[argument.index] : nullptr;
                        if (node && !node->children.empty())
                            render_trace_value_children(*node);
                        else
                            trace_value_text(argument.type + argument.value, argument.value, argument.readable);
                        if (argument.inspectable_reference && ImGui::SmallButton("Inspect reference")) {
                            std::uint64_t address = record.arguments[argument.index];
                            const bool by_ref = argument.index < trace.parameter_is_by_ref.size() &&
                                trace.parameter_is_by_ref[argument.index];
                            if (by_ref && argument.index < record.argument_byref_value_bytes.size()) {
                                const std::vector<std::uint8_t>& bytes =
                                    record.argument_byref_value_bytes[argument.index];
                                if (!bytes.empty())
                                    std::memcpy(&address, bytes.data(),
                                                std::min(bytes.size(), sizeof(address)));
                            }
                            enqueue_raw_reference_inspection(address);
                        }
                        if (state.show_raw_abi)
                            ImGui::TextDisabled("Raw ABI: %s", argument.raw_abi.c_str());
                        ImGui::Unindent();
                        ImGui::PopID();
                    }
                }
                const std::string raw_arguments =
                    MethodTraceFormat::raw_arguments(trace, record, true);
                if (!raw_arguments.empty() && ImGui::SmallButton("Copy raw arguments"))
                    ImGui::SetClipboardText(raw_arguments.c_str());

                ImGui::SeparatorText("Return value");
                if (!trace.captures_return) {
                    render_return_capture_notice(trace);
                } else {
                ImGui::TextDisabled("Type: %s", trace.return_type.empty() ? "<unknown>" : trace.return_type.c_str());
                trace_value_text(trace.return_type + result, result, record.return_readable);
                if (!record.return_node.children.empty())
                    render_trace_value_children(record.return_node);
                if (record.return_reference_token != 0) {
                    if (ImGui::SmallButton("Open in Object Inspector"))
                        enqueue_reference_inspection(record.return_reference_token);
                } else if (record.return_captured && trace.return_is_reference && record.return_rax != 0 &&
                           ImGui::SmallButton("Try inspect returned reference")) {
                    enqueue_raw_reference_inspection(record.return_rax);
                }
                if (record.return_captured && trace.return_is_reference && record.return_rax != 0) {
                    ImGui::SameLine();
                    const std::string pointer = MethodTraceFormat::address(record.return_rax);
                    if (ImGui::SmallButton("Copy returned ptr"))
                        ImGui::SetClipboardText(pointer.c_str());
                }
                if (state.show_raw_abi)
                    ImGui::TextDisabled("Raw ABI: %s", MethodTraceFormat::raw_result(trace, record).c_str());
                if (record.return_captured && ImGui::SmallButton("Copy raw return"))
                    ImGui::SetClipboardText(MethodTraceFormat::raw_result(trace, record).c_str());
                }

                if (state.show_addresses) {
                    ImGui::SeparatorText("Addresses");
                    const std::string caller_address = MethodTraceFormat::address(record.caller_address);
                    ImGui::Text("Caller: %s", caller_address.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy caller"))
                        ImGui::SetClipboardText(caller_address.c_str());
                    if (!trace.is_static) {
                        const std::string target_address = MethodTraceFormat::address(record.target_address);
                        ImGui::Text("Target: %s", target_address.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Copy target"))
                            ImGui::SetClipboardText(target_address.c_str());
                    }
                }
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void enqueue_method_trace_stop(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::SetMethodTrace;
    command.bool_value = false;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_method_trace_close(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::CloseMethodTrace;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

std::string trace_list_display_name(const MethodTracer::Snapshot& trace) {
    const std::string method = short_trace_type_name(trace.declaring_type) + "." + trace.method_name;
    const char* const dispatch = trace.is_static ? "static" : "instance";
    return method + " [" + dispatch + "; " + std::to_string(trace.parameter_types.size()) + " args]";
}

} // namespace

void render_method_traces(const Snapshot &snapshot) {

    if (snapshot.method_traces.empty()) {
        ImGui::TextDisabled("No traced methods. Use Trace next to a method to start one.");
        return;
    }
    static MethodTracer::TraceId selected = 0;
    static std::array<char, 128> trace_list_filter{};
    const auto selected_found = std::find_if(snapshot.method_traces.begin(), snapshot.method_traces.end(),
                                             [](const MethodTracer::Snapshot &trace) { return trace.id == selected; });
    if (selected_found == snapshot.method_traces.end())
        selected = snapshot.method_traces.front().id;

    const float trace_list_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 220.0f, 340.0f);
    ImGui::BeginChild("##method-trace-list", ImVec2(trace_list_width, 0.0f), true);
    ImGui::TextDisabled("TRACED METHODS");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##trace-list-filter", "Filter methods...", trace_list_filter.data(),
                             trace_list_filter.size());
    ImGui::Separator();
    for (const MethodTracer::Snapshot &trace : snapshot.method_traces) {
        const std::string display_name = trace_list_display_name(trace);
        if (trace_list_filter[0] != '\0' &&
            !contains_case_insensitive(display_name, trace_list_filter.data()))
            continue;
        const std::string label = std::string(trace.active ? "[REC] " : "[STOP] ") + display_name +
                                  "  (" + std::to_string(trace.total_calls) + ")";
        // Scope by trace id, not name: overloads share a method name.
        const std::string trace_id = "trace-" + std::to_string(trace.id);
        ImGui::PushID(trace_id.c_str());
        if (ImGui::Selectable(label.c_str(), selected == trace.id))
            selected = trace.id;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s.%s\n%s | %zu parameters | %llu calls", trace.declaring_type.c_str(), trace.method_name.c_str(),
                              trace.active ? "Recording" : "Stopped", trace.parameter_types.size(),
                              static_cast<unsigned long long>(trace.total_calls));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##method-trace-detail", ImVec2(0.0f, 0.0f), true);
    const MethodTracer::Snapshot *trace = &*std::find_if(snapshot.method_traces.begin(), snapshot.method_traces.end(),
                                                           [](const MethodTracer::Snapshot &entry) { return entry.id == selected; });
    ImGui::TextColored(trace->active ? ImVec4(0.60f, 0.68f, 0.60f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
                       "%s.%s", trace->declaring_type.c_str(), trace->method_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", trace->active ? "RECORDING" : "STOPPED");
    ImGui::SameLine();
    if (trace->active && ImGui::SmallButton("Stop"))
        enqueue_method_trace_stop(trace->id);
    if (!trace->active) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close"))
            enqueue_method_trace_close(trace->id);
    }
    render_method_trace(snapshot, *trace);
    ImGui::EndChild();
}

std::string short_field_component_name(std::string_view type_name) {
    const std::size_t separator = type_name.rfind('.');
    return separator == std::string_view::npos ? std::string(type_name) : std::string(type_name.substr(separator + 1));
}

std::string field_watch_csv(const Snapshot::FieldWatch& watch) {
    std::string out = "sequence,seconds,member_kind,source,alarm,previous,current\n";
    for (const Snapshot::FieldWatchEvent& event : watch.events) {
        out += std::to_string(event.sequence) + "," + trace_seconds_json(event.seconds_since_start) + ",";
        append_csv_value(out, watch.property ? "property" : "field");
        out += ",";
        append_csv_value(out, event.source);
        out += event.alarm_triggered ? ",true," : ",false,";
        append_csv_value(out, event.previous_value);
        out += ",";
        append_csv_value(out, event.current_value);
        out += "\n";
    }
    return out;
}

void render_field_watches(const Snapshot &snapshot) {
    if (snapshot.field_watches.empty()) {
        ImGui::TextDisabled("No value watches. Use Watch next to a readable field or property.");
        ImGui::TextWrapped("Values are sampled four times per second; numeric members also produce a chart and alarms.");
        return;
    }
    static std::uint64_t selected = 0;
    static std::array<char, 128> watch_list_filter{};
    const auto selected_found = std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(),
                                             [](const Snapshot::FieldWatch &watch) {
                                                 return watch.id == selected;
                                             });
    if (selected_found == snapshot.field_watches.end())
        selected = snapshot.field_watches.front().id;

    const float watch_list_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 220.0f, 340.0f);
    ImGui::BeginChild("##field-watch-list", ImVec2(watch_list_width, 0.0f), true);
    ImGui::TextDisabled("VALUE WATCHES");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##field-watch-filter", "Filter members...", watch_list_filter.data(),
                             watch_list_filter.size());
    ImGui::Separator();
    for (const Snapshot::FieldWatch &watch : snapshot.field_watches) {
        const std::string display_name = short_field_component_name(watch.component_type) + "." + watch.field_name;
        if (watch_list_filter[0] != '\0' &&
            !contains_case_insensitive(display_name, watch_list_filter.data()))
            continue;
        const std::string label = std::string(watch.alarm_active ? "[ALARM] " : watch.active ? "[REC] " : "[STOP] ") +
                                  display_name + (watch.property ? "  P" : "  F") +
                                  "  (" + std::to_string(watch.change_count) + ")";
        if (ImGui::Selectable(label.c_str(), selected == watch.id))
            selected = watch.id;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s.%s\n%s | %llu recorded changes", watch.component_type.c_str(), watch.field_name.c_str(),
                              watch.active ? "Watching" : "Stopped",
                              static_cast<unsigned long long>(watch.change_count));
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##field-watch-detail", ImVec2(0.0f, 0.0f), true);
    const Snapshot::FieldWatch *watch =
        &*std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(), [](const auto &entry) {
            return entry.id == selected;
        });
    ImGui::TextColored(watch->active ? ImVec4(0.60f, 0.68f, 0.60f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
                       "%s.%s", watch->component_type.c_str(), watch->field_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", watch->active ? "WATCHING" : "STOPPED");
    ImGui::SameLine();
    if (watch->active && ImGui::SmallButton("Stop"))
        enqueue_field_watch(watch->component_instance_id, static_cast<int>(watch->field_index), false,
                            watch->object_inspector_token, watch->property);
    if (!watch->active) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close"))
            enqueue_field_watch_close(watch->id);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear history"))
        enqueue_field_watch_clear(watch->id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy CSV"))
        ImGui::SetClipboardText(field_watch_csv(*watch).c_str());

    ImGui::SeparatorText("Live value");
    ImGui::TextWrapped("%s changed %llu time%s. Current value: %s",
                       watch->property ? "Property" : "Field",
                       static_cast<unsigned long long>(watch->change_count), watch->change_count == 1 ? "" : "s",
                       watch->current_value.empty() ? "<waiting for a sample>" : watch->current_value.c_str());
    if (!watch->current_reference.is_null && watch->current_reference.token != 0) {
        render_reference_context_menu(&watch->current_reference);
    }
    if (watch->setter_hooked)
        ImGui::TextDisabled("The property setter is hooked, so every write is reported with its caller. "
                            "Values are read back through the getter.");
    else if (watch->property)
        ImGui::TextDisabled("This property has no hookable setter, so rows are sampled runtime activity. "
                            "Only Explorer writes are exact.");
    else
        ImGui::TextDisabled("Direct field writes have no managed setter to hook. Values are sampled every frame, "
                            "so a write that lands and reverts within one frame is still missed.");

    ImGui::SeparatorText("Chart and threshold alarm");
    float& threshold = ui_state().watch_threshold_drafts.touch(watch->id);
    if (!ImGui::IsAnyItemActive())
        threshold = static_cast<float>(watch->alarm_threshold);
    int condition = static_cast<int>(watch->alarm_condition);
    constexpr const char* conditions[] = {"Disabled", ">", ">=", "<", "<=", "==", "!="};
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Condition", &condition, conditions, IM_ARRAYSIZE(conditions)))
        enqueue_watch_alarm(watch->id, static_cast<WatchAnalysis::AlarmCondition>(condition), threshold);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputFloat("Threshold", &threshold, 0.0f, 0.0f, "%.6g",
                          ImGuiInputTextFlags_EnterReturnsTrue))
        enqueue_watch_alarm(watch->id, static_cast<WatchAnalysis::AlarmCondition>(condition), threshold);
    if (watch->alarm_condition != WatchAnalysis::AlarmCondition::Disabled) {
        ImGui::SameLine();
        ImGui::TextColored(watch->alarm_active ? ImVec4(0.94f, 0.38f, 0.30f, 1.0f)
                                               : ImVec4(0.55f, 0.72f, 0.55f, 1.0f),
                           "%s  (%llu trigger%s)", watch->alarm_active ? "ALARM" : "armed",
                           static_cast<unsigned long long>(watch->alarm_count), watch->alarm_count == 1 ? "" : "s");
    }
    if (!watch->samples.empty()) {
        ImGui::PlotLines("##watch-chart", &watch->samples.front().value,
                         static_cast<int>(watch->samples.size()), 0, nullptr, FLT_MAX, FLT_MAX,
                         ImVec2(-1.0f, 120.0f), sizeof(Snapshot::FieldWatchSample));
        ImGui::TextDisabled("%zu numeric samples | %.2fs -> %.2fs", watch->samples.size(),
                            watch->samples.front().seconds_since_start, watch->samples.back().seconds_since_start);
    } else {
        ImGui::TextDisabled("A chart becomes available when this member produces numeric samples.");
    }
    if (watch->events.empty()) {
        ImGui::TextDisabled(watch->active ? "Waiting for the value to change..." : "No changes were recorded.");
        ImGui::EndChild();
        return;
    }
    if (ImGui::BeginTable("##field-watch-events", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(150.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Alarm", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Old value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("New value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t displayed = 0; displayed < watch->events.size(); ++displayed) {
            const Snapshot::FieldWatchEvent &event = watch->events[watch->events.size() - 1 - displayed];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", static_cast<unsigned long long>(event.sequence));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("+%.3fs", event.seconds_since_start);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(event.source.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(event.alarm_triggered ? ImVec4(0.94f, 0.38f, 0.30f, 1.0f)
                                                     : ImVec4(0.52f, 0.52f, 0.52f, 1.0f),
                               "%s", event.alarm_triggered ? "YES" : "-");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(event.previous_value.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(event.current_value.c_str());
            if (!event.current_reference.is_null && event.current_reference.token != 0) {
                ImGui::PushID(static_cast<int>(event.sequence));
                render_reference_context_menu(&event.current_reference);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace Explorer::UI
