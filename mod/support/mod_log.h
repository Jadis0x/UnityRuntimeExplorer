// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/mod_sdk.h"

namespace ModLog {
void initialize(const URK_ModContext* ctx);
const URK_ModContext* context();
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);
void success(const char* fmt, ...);
void shutdown();
} // namespace ModLog
