#pragma once

#include "runtime_api.h"
#include "sdk/runtime/managed_runtime.h"

namespace URK {
inline bool initialize_backend(const ModContext* context) {
  return URK::managed::init(context);
}
} // namespace URK
