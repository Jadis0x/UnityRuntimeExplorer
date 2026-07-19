#pragma once

#include "runtime_api.h"
#include "sdk/il2cpp/il2cpp_runtime.h"

namespace URK {
inline bool initialize_backend(const ModContext* context) {
  return URK::il2cpp::init(context);
}
} // namespace URK
