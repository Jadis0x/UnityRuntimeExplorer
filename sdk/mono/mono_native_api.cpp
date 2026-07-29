#include "mono_native_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace URK::mono::native {
namespace {

using AssemblyCallback = void (*)(void*, void*);
using AssemblyForeachFn = void (*)(AssemblyCallback, void*);
using ClassGetImageFn = const void* (*)(const void*);
using ClassGetElementClassFn = const void* (*)(const void*);
using ClassValueSizeFn = std::int32_t (*)(const void*, std::uint32_t*);
using ClassIsAssignableFromFn = int (*)(const void*, const void*);
using ClassEnumBaseTypeFn = const void* (*)(const void*);
using MethodIsGenericFn = int (*)(const void*);
using MethodGetParamNamesFn = void (*)(const void*, const char**);
using FieldGetParentFn = const void* (*)(const void*);
using FieldGetValueObjectFn = void* (*)(const void*, const void*, void*);
using StringNewLenFn = void* (*)(const void*, const char*, std::uint32_t);
using ObjectToStringFn = void* (*)(void*, void**);
using StringToUtf8Fn = char* (*)(void*);
using FreeFn = void (*)(void*);
using GcSizeFn = std::int64_t (*)();

struct Exports {
    HMODULE module = nullptr;
    AssemblyForeachFn assembly_foreach = nullptr;
    ClassGetImageFn class_get_image = nullptr;
    ClassGetElementClassFn class_get_element_class = nullptr;
    ClassValueSizeFn class_value_size = nullptr;
    ClassIsAssignableFromFn class_is_assignable_from = nullptr;
    ClassEnumBaseTypeFn class_enum_basetype = nullptr;
    MethodIsGenericFn method_is_generic = nullptr;
    MethodGetParamNamesFn method_get_param_names = nullptr;
    FieldGetParentFn field_get_parent = nullptr;
    FieldGetValueObjectFn field_get_value_object = nullptr;
    StringNewLenFn string_new_len = nullptr;
    ObjectToStringFn object_to_string = nullptr;
    StringToUtf8Fn string_to_utf8 = nullptr;
    FreeFn free = nullptr;
    GcSizeFn gc_get_used_size = nullptr;
    GcSizeFn gc_get_heap_size = nullptr;
};

struct State {
    std::mutex mutex;
    Exports exports;
    std::vector<const void*> assemblies;
};

State& state() {
    static State value;
    return value;
}

std::string& error_slot() {
    thread_local std::string value;
    return value;
}

void set_error(const char* text) {
    error_slot() = text ? text : "";
}

template <class Function>
Function resolve(HMODULE module, const char* name) {
    return module && name
        ? reinterpret_cast<Function>(GetProcAddress(module, name))
        : nullptr;
}

HMODULE mono_module(const URK::ModContext* context) {
    if (context && context->size >=
            offsetof(URK::ModContext, runtimeBackendModuleBase) +
                sizeof(context->runtimeBackendModuleBase) &&
        context->runtimeBackendModuleBase != 0) {
        return reinterpret_cast<HMODULE>(context->runtimeBackendModuleBase);
    }

    constexpr const wchar_t* candidates[] = {
        L"mono-2.0-bdwgc.dll",
        L"mono.dll",
        L"libmono.dll",
    };
    for (const wchar_t* candidate : candidates) {
        if (HMODULE module = GetModuleHandleW(candidate))
            return module;
    }
    return nullptr;
}

void collect_assembly(void* assembly, void* user_data) {
    if (assembly && user_data)
        static_cast<std::vector<const void*>*>(user_data)->push_back(assembly);
}

} // namespace

void initialize(const URK::ModContext* context) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    current.exports = {};
    current.assemblies.clear();
    error_slot().clear();

    HMODULE module = mono_module(context);
    if (!module) {
        set_error("Mono runtime module could not be located");
        return;
    }

    Exports exports{};
    exports.module = module;
    exports.assembly_foreach =
        resolve<AssemblyForeachFn>(module, "mono_assembly_foreach");
    exports.class_get_image =
        resolve<ClassGetImageFn>(module, "mono_class_get_image");
    exports.class_get_element_class =
        resolve<ClassGetElementClassFn>(module, "mono_class_get_element_class");
    exports.class_value_size =
        resolve<ClassValueSizeFn>(module, "mono_class_value_size");
    exports.class_is_assignable_from =
        resolve<ClassIsAssignableFromFn>(module, "mono_class_is_assignable_from");
    exports.class_enum_basetype =
        resolve<ClassEnumBaseTypeFn>(module, "mono_class_enum_basetype");
    exports.method_is_generic =
        resolve<MethodIsGenericFn>(module, "mono_method_is_generic");
    exports.method_get_param_names =
        resolve<MethodGetParamNamesFn>(module, "mono_method_get_param_names");
    exports.field_get_parent =
        resolve<FieldGetParentFn>(module, "mono_field_get_parent");
    exports.field_get_value_object =
        resolve<FieldGetValueObjectFn>(module, "mono_field_get_value_object");
    exports.string_new_len =
        resolve<StringNewLenFn>(module, "mono_string_new_len");
    exports.object_to_string =
        resolve<ObjectToStringFn>(module, "mono_object_to_string");
    exports.string_to_utf8 =
        resolve<StringToUtf8Fn>(module, "mono_string_to_utf8");
    exports.free = resolve<FreeFn>(module, "mono_free");
    exports.gc_get_used_size =
        resolve<GcSizeFn>(module, "mono_gc_get_used_size");
    exports.gc_get_heap_size =
        resolve<GcSizeFn>(module, "mono_gc_get_heap_size");
    current.exports = exports;
}

void reset() {
    State& current = state();
    std::lock_guard lock(current.mutex);
    current.exports = {};
    current.assemblies.clear();
    error_slot().clear();
}

bool available() {
    State& current = state();
    std::lock_guard lock(current.mutex);
    return current.exports.module != nullptr;
}

const char* last_error() {
    return error_slot().empty() ? nullptr : error_slot().c_str();
}

void clear_error() {
    error_slot().clear();
}

std::size_t refresh_assemblies() {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    current.assemblies.clear();
    if (!current.exports.assembly_foreach) {
        set_error("Mono export mono_assembly_foreach is unavailable");
        return 0;
    }
    current.exports.assembly_foreach(&collect_assembly, &current.assemblies);
    std::erase(current.assemblies, nullptr);
    std::sort(current.assemblies.begin(), current.assemblies.end());
    current.assemblies.erase(
        std::unique(current.assemblies.begin(), current.assemblies.end()),
        current.assemblies.end());
    return current.assemblies.size();
}

const void* assembly_at(std::size_t index) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (index >= current.assemblies.size()) {
        set_error("Mono assembly index is outside the latest domain snapshot");
        return nullptr;
    }
    return current.assemblies[index];
}

const void* class_get_image(const void* klass) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!klass) {
        set_error("Mono class_get_image requires a class");
        return nullptr;
    }
    if (!current.exports.class_get_image) {
        set_error("Mono export mono_class_get_image is unavailable");
        return nullptr;
    }
    return current.exports.class_get_image(klass);
}

const void* class_get_element_class(const void* klass) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!klass) {
        set_error("Mono class_get_element_class requires a class");
        return nullptr;
    }
    if (!current.exports.class_get_element_class) {
        set_error("Mono export mono_class_get_element_class is unavailable");
        return nullptr;
    }
    return current.exports.class_get_element_class(klass);
}

std::int32_t class_value_size(const void* klass, std::uint32_t* alignment) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!klass) {
        set_error("Mono class_value_size requires a class");
        return -1;
    }
    if (!current.exports.class_value_size) {
        set_error("Mono export mono_class_value_size is unavailable");
        return -1;
    }
    return current.exports.class_value_size(klass, alignment);
}

int class_is_assignable_from(const void* target, const void* candidate) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!target || !candidate) {
        set_error("Mono class_is_assignable_from requires two classes");
        return 0;
    }
    if (!current.exports.class_is_assignable_from) {
        set_error("Mono export mono_class_is_assignable_from is unavailable");
        return 0;
    }
    return current.exports.class_is_assignable_from(target, candidate);
}

const void* class_enum_basetype(const void* klass) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!klass) {
        set_error("Mono class_enum_basetype requires a class");
        return nullptr;
    }
    if (!current.exports.class_enum_basetype) {
        set_error("Mono export mono_class_enum_basetype is unavailable");
        return nullptr;
    }
    return current.exports.class_enum_basetype(klass);
}

bool method_is_generic(const void* method) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!method) {
        set_error("Mono method_is_generic requires a method");
        return false;
    }
    if (!current.exports.method_is_generic) {
        set_error("Mono export mono_method_is_generic is unavailable");
        return false;
    }
    return current.exports.method_is_generic(method) != 0;
}

const char* method_get_param_name(const void* method, std::uint32_t index,
                                  std::uint32_t parameter_count) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!method || index >= parameter_count) {
        set_error("Mono method parameter name index is invalid");
        return nullptr;
    }
    if (!current.exports.method_get_param_names) {
        set_error("Mono export mono_method_get_param_names is unavailable");
        return nullptr;
    }
    thread_local std::vector<const char*> names;
    names.assign(parameter_count, nullptr);
    current.exports.method_get_param_names(method, names.data());
    return names[index];
}

const void* field_get_parent(const void* field) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!field) {
        set_error("Mono field_get_parent requires a field");
        return nullptr;
    }
    if (!current.exports.field_get_parent) {
        set_error("Mono export mono_field_get_parent is unavailable");
        return nullptr;
    }
    return current.exports.field_get_parent(field);
}

void* field_get_value_object(const void* domain, const void* field, void* object) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!domain || !field) {
        set_error("Mono field_get_value_object requires a domain and field");
        return nullptr;
    }
    if (!current.exports.field_get_value_object) {
        set_error("Mono export mono_field_get_value_object is unavailable");
        return nullptr;
    }
    return current.exports.field_get_value_object(domain, field, object);
}

void* string_new_len(const void* domain, const char* utf8, std::uint32_t length) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!domain || !utf8) {
        set_error("Mono string_new_len requires a domain and UTF-8 input");
        return nullptr;
    }
    if (!current.exports.string_new_len) {
        set_error("Mono export mono_string_new_len is unavailable");
        return nullptr;
    }
    return current.exports.string_new_len(domain, utf8, length);
}

std::string exception_message(void* exception) {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!exception) {
        set_error("Mono exception formatting requires an exception object");
        return {};
    }
    if (!current.exports.object_to_string || !current.exports.string_to_utf8 ||
        !current.exports.free) {
        set_error("Mono exception formatting exports are unavailable");
        return {};
    }

    void* formatting_exception = nullptr;
    void* managed_text = current.exports.object_to_string(exception, &formatting_exception);
    if (formatting_exception || !managed_text) {
        set_error("Mono exception ToString() failed");
        return {};
    }
    char* utf8 = current.exports.string_to_utf8(managed_text);
    if (!utf8) {
        set_error("Mono exception UTF-8 conversion failed");
        return {};
    }
    std::string message(utf8);
    current.exports.free(utf8);
    return message;
}

std::int64_t gc_get_used_size() {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!current.exports.gc_get_used_size) {
        set_error("Mono export mono_gc_get_used_size is unavailable");
        return 0;
    }
    return current.exports.gc_get_used_size();
}

std::int64_t gc_get_heap_size() {
    State& current = state();
    std::lock_guard lock(current.mutex);
    error_slot().clear();
    if (!current.exports.gc_get_heap_size) {
        set_error("Mono export mono_gc_get_heap_size is unavailable");
        return 0;
    }
    return current.exports.gc_get_heap_size();
}

} // namespace URK::mono::native
