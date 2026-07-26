// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <string>

namespace ModConfig::UserSettings {

bool load();
bool save_toggle_key(int virtual_key);
void begin_toggle_key_capture();
int poll_toggle_key_capture();
void end_toggle_key_capture();
std::string virtual_key_name(int virtual_key);
const std::string& last_error();

} // namespace ModConfig::UserSettings
