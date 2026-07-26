#pragma once

#if defined(URK_BACKEND_MONO)
#include "../mono/mono_runtime.h"
namespace URK {
namespace managed = mono;
inline constexpr RuntimeBackend compiled_runtime_backend = runtime_backend_mono;
inline constexpr const char* compiled_runtime_name = "Mono";
} // namespace URK
#elif defined(URK_BACKEND_IL2CPP)
#include "../il2cpp/il2cpp_runtime.h"
namespace URK {
namespace managed = il2cpp;
inline constexpr RuntimeBackend compiled_runtime_backend = runtime_backend_il2cpp;
inline constexpr const char* compiled_runtime_name = "IL2CPP";
} // namespace URK
#else
#error "Exactly one managed runtime backend must be selected."
#endif
