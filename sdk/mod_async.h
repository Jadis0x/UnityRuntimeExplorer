#pragma once

#include "coroutines.h"

namespace ModAsync {
URK::coroutines::FlowState& flow();
void spawn(URK::coroutines::Task task);
void cancel_all();
} // namespace ModAsync
