#include "sdk/mono/mono_runtime.h"
#include "sdk/unity/unity_inspect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

static_assert(URK_MONO_API_VERSION == 8,
              "The loader-facing Mono table must match the supplied SDK");
static_assert(sizeof(URK_MonoApi) ==
                  offsetof(URK_MonoApi, method_is_generic) +
                      sizeof(((URK_MonoApi*)nullptr)->method_is_generic),
              "No fields may be read beyond the supplied Mono v8 table");

namespace {

const auto class_handle = reinterpret_cast<const void*>(0x1000);
const auto image_handle = reinterpret_cast<const void*>(0x2000);
const auto first_image_class = reinterpret_cast<const void*>(0x3000);
const auto second_image_class = reinterpret_cast<const void*>(0x4000);
const auto string_handle = reinterpret_cast<void*>(0x5000);
const auto incomplete_method_handle = reinterpret_cast<const void*>(0x6000);

std::uint32_t observed_class_token = 0;
char observed_string[64]{};

int attach_current_thread() {
    return 1;
}

int attach_current_thread_fails() {
    return 0;
}

// A two-class chain that loops back on itself, plus a class whose method
// iterator never terminates: both model metadata an obfuscator can emit.
const auto cyclic_child = reinterpret_cast<const void*>(0x7000);
const auto cyclic_parent = reinterpret_cast<const void*>(0x7100);
const auto endless_class = reinterpret_cast<const void*>(0x7200);
const auto endless_method = reinterpret_cast<const void*>(0x7300);
const auto endless_field = reinterpret_cast<const void*>(0x7400);

const void* class_get_parent(const void* klass) {
    if (klass == cyclic_child)
        return cyclic_parent;
    if (klass == cyclic_parent)
        return cyclic_child;
    return nullptr;
}

const void* class_get_methods(const void* klass, void** iterator) {
    if (klass != endless_class || !iterator)
        return nullptr;
    // Never advances and never returns null.
    return endless_method;
}

const void* class_get_fields(const void* klass, void** iterator) {
    if (klass != endless_class || !iterator)
        return nullptr;
    return endless_field;
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

const char* method_get_name(const void* method) {
    if (method == incomplete_method_handle)
        return "<SetupOutfit>b__0";
    return method == endless_method ? "NotTheOneWeWant" : nullptr;
}

const char* field_get_name(const void* field) {
    return field == endless_field ? "notTheOneWeWant" : nullptr;
}

const void* method_get_return_type(const void*) {
    // Models stripped or otherwise unreadable generated signature metadata.
    return nullptr;
}

std::uint32_t method_get_flags(const void*, std::uint32_t* implementation_flags) {
    if (implementation_flags)
        *implementation_flags = 0;
    return 0;
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
    mono.method_get_name = &method_get_name;
    mono.method_get_return_type = &method_get_return_type;
    mono.method_get_flags = &method_get_flags;
    mono.class_get_parent = &class_get_parent;
    mono.class_get_methods = &class_get_methods;
    mono.class_get_fields = &class_get_fields;
    mono.field_get_name = &field_get_name;

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

    URK::Unity::Inspect::TypeInfo generated_declaring{};
    generated_declaring.handle = class_handle;
    generated_declaring.namespc = "Example";
    generated_declaring.name = "<>c__DisplayClass1_0";
    generated_declaring.full_name = "Example.<>c__DisplayClass1_0";
    const auto strict_method = URK::Unity::Inspect::method_info(
        static_cast<const URK::managed::Method*>(incomplete_method_handle), generated_declaring);
    ok &= require(strict_method.handle == nullptr,
                  "strict method metadata reads must still reject incomplete signatures");
    const auto navigable_method = URK::Unity::Inspect::method_info(
        static_cast<const URK::managed::Method*>(incomplete_method_handle), generated_declaring, true);
    ok &= require(navigable_method.handle == incomplete_method_handle,
                  "generated methods must remain navigable when their signature is incomplete");
    ok &= require(!navigable_method.signature_complete,
                  "an unreadable generated return type must be marked metadata-only");
    ok &= require(navigable_method.return_type == "<unknown>",
                  "metadata-only methods need a stable unknown return-type label");

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

    // A parent chain that loops must terminate the walk instead of hanging the
    // caller: the explorer runs these on the Unity main thread, where the
    // structured-exception handler catches faults but never a hang.
    ok &= require(URK::mono::resolve_method(
                      static_cast<const URK::mono::Class*>(cyclic_child), "Missing", 0) == nullptr,
                  "a cyclic parent chain must not trap resolve_method");
    const char* const no_parameters[] = {""};
    ok &= require(URK::mono::resolve_method_exact(
                      static_cast<const URK::mono::Class*>(cyclic_child), "Missing",
                      no_parameters, 0) == nullptr,
                  "a cyclic parent chain must not trap resolve_method_exact");
    ok &= require(URK::mono::resolve_field(
                      static_cast<const URK::mono::Class*>(cyclic_child), "missing") == nullptr,
                  "a cyclic parent chain must not trap resolve_field");
    ok &= require(URK::mono::resolve_method(
                      static_cast<const URK::mono::Class*>(endless_class), "Missing", 0) == nullptr,
                  "a non-advancing method iterator must not trap resolve_method");
    ok &= require(URK::mono::resolve_field(
                      static_cast<const URK::mono::Class*>(endless_class), "missing") == nullptr,
                  "a non-advancing field iterator must not trap resolve_field");

    // A rejected init() must close the backend. api() still resolves so
    // last_error() can explain the rejection, so available() is the only gate.
    URK_MonoApi unattachable = mono;
    unattachable.attach_current_thread = &attach_current_thread_fails;
    URK_ModContext unattachable_context = context;
    unattachable_context.mono = &unattachable;
    ok &= require(!URK::mono::init(&unattachable_context),
                  "a failed thread attach must reject the context");
    ok &= require(!URK::mono::available(),
                  "a failed thread attach must leave the backend unavailable");
    ok &= require(URK::mono::find_class("Assembly-CSharp.dll", "Example", "Player") == nullptr,
                  "a rejected context must not keep serving managed lookups");

    // Same for a context the loader truncated below the compiled struct.
    URK_ModContext truncated = context;
    truncated.size = static_cast<std::uint32_t>(
        offsetof(URK_ModContext, runtimeCapabilities) + sizeof(truncated.runtimeCapabilities));
    ok &= require(!URK::mono::init(&truncated),
                  "a truncated mod context must be rejected");
    ok &= require(!URK::mono::available(),
                  "a truncated mod context must leave the backend unavailable");
    ok &= require(URK::mono::find_class("Assembly-CSharp.dll", "Example", "Player") == nullptr,
                  "a truncated mod context must not keep serving managed lookups");

    URK::mono::init(nullptr);
    ok &= require(!URK::mono::available(),
                  "clearing the context must invalidate the backend");
    return ok ? 0 : 1;
}
