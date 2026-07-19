// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_log.h"

#include "config/mod_config.h"

#include <cstdarg>
#include <cstdio>

namespace {
const URK_ModContext* g_ctx = nullptr;

void write_log(const char* level, const char* fmt, va_list args) {
  if (!g_ctx || !g_ctx->Log) return;

  char message[1600]{};
  if (fmt && fmt[0])
    std::vsnprintf(message, sizeof(message), fmt, args);

  if (level && level[0])
    g_ctx->Log("[%s][%s] %s", ModConfig::display_name, level, message);
  else
    g_ctx->Log("[%s] %s", ModConfig::display_name, message);
}
} // namespace

namespace ModLog {
void initialize(const URK_ModContext* ctx) { g_ctx = ctx; }
const URK_ModContext* context() { return g_ctx; }

void info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write_log("info", fmt, args);
  va_end(args);
}

void warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write_log("warn", fmt, args);
  va_end(args);
}

void error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write_log("error", fmt, args);
  va_end(args);
}

void success(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  write_log("success", fmt, args);
  va_end(args);
}

void shutdown() { g_ctx = nullptr; }
} // namespace ModLog
