#include "sdk/mono/mono_runtime.h"
#include "sdk/unity/unity_inspect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

static_assert(URK_MONO_API_VERSION == 7,
              "The loader-facing Mono table must match the supplied SDK");
static_assert(sizeof(URK_MonoApi) ==
                  offsetof(URK_MonoApi, value_box) +
                      sizeof(((URK_MonoApi*)nullptr)->value_box),
              "No fields may be read beyond the supplied Mono v7 table");

namespace {

const auto class_handle = reinterpret_cast<const void*>(0x1000);
const auto image_handle = reinterpret_cast<const void*>(0x2000);
const auto first_image_class = reinterpret_cast<const void*>(0x3000);
const auto second_image_class = reinterpret_cast<const void*>(0x4000);
const auto string_handle = reinterpret_cast<void*>(0x5000);

std::uint32_t observed_class_token = 0;
char observed_string[64]{};

int attach_current_thread() {
    return 1;
}

const void* find_class(const char*, const char*, const char*) {
    return class_handle;
}

const void* find_image(const char*) {
    return image_handle;
}

int image_get_table_rows(const void* image, int table) {
    return image == image_handle && table == 2 ? 2 : 0;
}

const void* image_get_class(const void* image, std::uint32_t token) {
    if (image != image_handle)
        return nullptr;
    observed_class_token = token;
    if (token == 0x02000001u)
        return first_image_class;
    if (token == 0x02000002u)
        return second_image_class;
    return nullptr;
}

void* new_string(const char* utf8) {
    std::snprintf(observed_string, sizeof(observed_string), "%s",
                  utf8 ? utf8 : "");
    return string_handle;
}

const char* last_error() {
    return nullptr;
}

bool require(bool condition, const char* message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

} // namespace

int main() {
    URK_MonoApi mono{};
    mono.version = URK_MONO_API_VERSION;
    mono.size = sizeof(mono);
    mono.attach_current_thread = &attach_current_thread;
    mono.find_class = &find_class;
    mono.find_image = &find_image;
    mono.image_get_table_rows = &image_get_table_rows;
    mono.image_get_class = &image_get_class;
    mono.new_string = &new_string;
    mono.last_error = &last_error;

    URK_ModContext context{};
    context.version = URK_SDK_MIN_COMPAT_VERSION;
    context.size = sizeof(context);
    context.runtimeBackend = URK_RUNTIME_BACKEND_MONO;
    context.runtimeCapabilities = URK_RUNTIME_CAP_MONO_API;
    context.runtimeBackendModuleBase =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    context.mono = &mono;

    bool ok = true;
    ok &= require(URK::mono::init(&context),
                  "a generated Mono v7 table must initialize");
    ok &= require(URK::mono::available(),
                  "a valid v7 table must report available");

    const auto* klass = URK::mono::find_class(
        "Assembly-CSharp.dll", "Example", "Player");
    ok &= require(klass == class_handle,
                  "class lookup must use the supplied v7 table");
    ok &= require(URK::mono::class_get_image(klass) == image_handle,
                  "class lookup must retain its image when class_get_image is not exported");

    ok &= require(URK::mono::image_get_class_count(
                      static_cast<const URK::mono::Image*>(image_handle)) == 2,
                  "Mono TYPEDEF rows must drive class enumeration");
    ok &= require(URK::mono::image_get_class(
                      static_cast<const URK::mono::Image*>(image_handle), 1) ==
                      second_image_class,
                  "class enumeration must translate a zero-based index to a Mono TYPEDEF token");
    ok &= require(observed_class_token == 0x02000002u,
                  "Mono TYPEDEF token translation is incorrect");

    ok &= require(URK::mono::string_new_len("mono", 4) == string_handle,
                  "string_new_len must fall back to the v7 new_string entry");
    ok &= require(std::strcmp(observed_string, "mono") == 0,
                  "v7 string fallback changed the UTF-8 payload");

    URK::Unity::Inspect::MethodParamInfo boolean_parameter{};
    boolean_parameter.type = reinterpret_cast<const void*>(0x6000);
    boolean_parameter.type_name = "System.Boolean";
    boolean_parameter.is_value_type = true;
    URK::Unity::Inspect::ValueInfo boolean_argument{};
    boolean_argument.kind = URK::Unity::Inspect::ValueKind::Boolean;
    boolean_argument.bool_value = true;
    URK::Unity::Inspect::WriteStorage boolean_storage{};
    void* boolean_pointer = nullptr;
    ok &= require(URK::Unity::Inspect::method_argument_pointer(
                      boolean_parameter, boolean_argument, boolean_storage,
                      boolean_pointer),
                  "a Mono Boolean parameter must marshal as a scalar");
    ok &= require(boolean_pointer == &boolean_storage.b && boolean_storage.b,
                  "Boolean parameter marshalling must preserve the value");

    ok &= require(URK::mono::domain_get_assembly_count() == 0,
                  "a process without Mono exports must not invent assemblies");
    const char* error = URK::mono::last_error();
    ok &= require(error && std::strstr(error, "mono_assembly_foreach"),
                  "missing advanced Mono exports must remain observable");

    URK::mono::init(nullptr);
    ok &= require(!URK::mono::available(),
                  "clearing the context must invalidate the backend");
    return ok ? 0 : 1;
}
