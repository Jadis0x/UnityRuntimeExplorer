#pragma once

#include "mono_runtime.h"

#include <cstdio>
#include <cstring>

namespace URK::mono::helpers {

using DiagnosticSink = void (*)(const char*);

inline void emit(DiagnosticSink sink, const char* message) {
  if (sink && message)
    sink(message);
}

inline void emit_last_error(DiagnosticSink sink) {
  if (const char* error = URK::mono::last_error(); error && error[0])
    emit(sink, error);
}

inline bool require_class(const char* image, const char* namespc,
                          const char* name, const URK::mono::Class*& output,
                          DiagnosticSink sink = nullptr) {
  output = URK::mono::find_class(image, namespc, name);
  if (output)
    return true;
  char message[512]{};
  std::snprintf(message, sizeof(message),
                "[URK Mono SDK] missing class: %s:%s.%s",
                image ? image : "", namespc ? namespc : "", name ? name : "");
  emit(sink, message);
  emit_last_error(sink);
  return false;
}

inline bool require_method_exact(
    const char* image, const char* namespc, const char* klass,
    const char* name, const char* const* parameter_types, int parameter_count,
    const URK::mono::Method*& output, DiagnosticSink sink = nullptr) {
  const auto* type = URK::mono::find_class(image, namespc, klass);
  output = type ? URK::mono::resolve_method_exact(
                      type, name, parameter_types, parameter_count)
                : nullptr;
  if (output)
    return true;

  char parameters[384]{};
  std::size_t used = 0;
  for (int index = 0; index < parameter_count && used < sizeof(parameters);
       ++index) {
    const int written = std::snprintf(
        parameters + used, sizeof(parameters) - used, "%s%s",
        index ? ", " : "",
        parameter_types && parameter_types[index]
            ? parameter_types[index]
            : "<unknown>");
    if (written < 0)
      break;
    used += static_cast<std::size_t>(written);
  }

  char message[1024]{};
  std::snprintf(message, sizeof(message),
                "[URK Mono SDK] missing/changed method: %s:%s.%s.%s(%s)",
                image ? image : "", namespc ? namespc : "",
                klass ? klass : "", name ? name : "", parameters);
  emit(sink, message);
  emit_last_error(sink);
  return false;
}

inline bool require_method(const char* image, const char* namespc,
                           const char* klass, const char* name, int argc,
                           const URK::mono::Method*& output,
                           DiagnosticSink sink = nullptr) {
  const auto* type = URK::mono::find_class(image, namespc, klass);
  output = type ? URK::mono::resolve_method(type, name, argc) : nullptr;
  if (output)
    return true;
  char message[640]{};
  std::snprintf(message, sizeof(message),
                "[URK Mono SDK] missing method: %s:%s.%s.%s/%d",
                image ? image : "", namespc ? namespc : "",
                klass ? klass : "", name ? name : "", argc);
  emit(sink, message);
  emit_last_error(sink);
  return false;
}

inline bool require_field(const char* image, const char* namespc,
                          const char* klass, const char* name,
                          const URK::mono::Field*& output,
                          DiagnosticSink sink = nullptr) {
  const auto* type = URK::mono::find_class(image, namespc, klass);
  output = type ? URK::mono::resolve_field(type, name) : nullptr;
  if (output)
    return true;
  char message[640]{};
  std::snprintf(message, sizeof(message),
                "[URK Mono SDK] missing/changed field: %s:%s.%s.%s",
                image ? image : "", namespc ? namespc : "",
                klass ? klass : "", name ? name : "");
  emit(sink, message);
  emit_last_error(sink);
  return false;
}

inline bool require_property(const URK::mono::Class* klass, const char* name,
                             const URK::mono::Property*& output,
                             DiagnosticSink sink = nullptr) {
  void* iterator = nullptr;
  while ((output = URK::mono::class_get_properties(klass, &iterator)) !=
         nullptr) {
    const char* candidate = URK::mono::property_get_name(output);
    if (candidate && name && std::strcmp(candidate, name) == 0)
      return true;
  }
  char message[512]{};
  std::snprintf(message, sizeof(message),
                "[URK Mono SDK] missing property: %s", name ? name : "");
  emit(sink, message);
  emit_last_error(sink);
  return false;
}

} // namespace URK::mono::helpers
