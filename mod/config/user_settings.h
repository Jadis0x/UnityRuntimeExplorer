// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <string>

namespace ModConfig::UserSettings {

bool load();
bool save_toggle_key(int virtual_key);
bool save_mcp_enabled(bool enabled);
bool save_mcp_auto_discovery(bool enabled);
bool save_mcp_property_access(bool enabled);
bool save_mcp_writes(bool enabled);
bool save_mcp_tracing(bool enabled);
bool save_mcp_invocation(bool enabled);
bool save_mcp_destructive_operations(bool enabled);
bool save_mcp_full_access(bool enabled);
// Values are clamped to the UI ranges.
bool save_mcp_pacing(int requests_per_second, int burst, int timeout_ms);
void begin_toggle_key_capture();
int poll_toggle_key_capture();
void end_toggle_key_capture();
std::string virtual_key_name(int virtual_key);
// Overlay text scale and panel background opacity, remembered between sessions.
float ui_scale();
float panel_opacity();
bool save_appearance(float ui_scale, float panel_opacity);
const std::string& last_error();

} // namespace ModConfig::UserSettings
