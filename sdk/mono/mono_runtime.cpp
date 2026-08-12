#include "mono_runtime.h"
#include "mono_native_api.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace URK::mono {
namespace {

const URK_MonoApi*& api_slot() {
    static const URK_MonoApi* value = nullptr;
    return value;
}

std::string& invocation_error_slot() {
    thread_local std::string value;
    return value;
}

bool same_type_name(const Type* type, const char* requested) {
    if (!type || !requested)
        return false;
    char name[512]{};
    return type_get_name(type, name, sizeof(name)) && std::string_view{name} == requested;
}

std::mutex& class_image_mutex() {
    static std::mutex value;
    return value;
}

std::unordered_map<const Class*, const Image*>& class_images() {
    static std::unordered_map<const Class*, const Image*> value;
    return value;
}

void remember_class_image(const Class* klass, const Image* image) {
    if (!klass || !image)
        return;
    std::lock_guard lock(class_image_mutex());
    class_images()[klass] = image;
}

const Image* remembered_class_image(const Class* klass) {
    std::lock_guard lock(class_image_mutex());
    const auto found = class_images().find(klass);
    return found == class_images().end() ? nullptr : found->second;
}

} // namespace

bool init(const URK::ModContext* context) {
    URK::set_context(context);
    api_slot() = nullptr;
    invocation_error_slot().clear();
    native::reset();
    {
        std::lock_guard lock(class_image_mutex());
        class_images().clear();
    }
    if (!context || context->version < URK_SDK_MIN_COMPAT_VERSION ||
        context->size < sizeof(URK_ModContext) ||
        context->runtimeBackend != URK::runtime_backend_mono ||
        !URK::has_runtime_capability(URK::runtime_cap_mono_api) ||
        !context->mono || context->mono->version < URK_MONO_API_VERSION ||
        context->mono->size < sizeof(URK_MonoApi))
        return false;

    api_slot() = context->mono;
    native::initialize(context);
    const auto* value = api_slot();
    if (value->attach_current_thread && value->attach_current_thread() == 0) {
        native::reset();
        api_slot() = nullptr;
        return false;
    }
    return true;
}

const URK_MonoApi* api() {
    if (const URK::ModContext* context = URK::context()) {
        if (context->runtimeBackend == URK::runtime_backend_mono &&
            URK::has_runtime_capability(URK::runtime_cap_mono_api) &&
            context->mono && context->mono->version >= URK_MONO_API_VERSION &&
            context->mono->size >= sizeof(URK_MonoApi))
            return context->mono;
    }
    return api_slot();
}

bool available() {
    const auto* value = api();
    return value && value->version >= URK_MONO_API_VERSION &&
           value->size >= sizeof(URK_MonoApi);
}

const char* last_error() {
    const auto* value = api();
    if (value && value->last_error) {
        if (const char* error = value->last_error(); error && error[0]) {
            if (std::strstr(error, "runtime_invoke returned a managed exception") &&
                !invocation_error_slot().empty())
                return invocation_error_slot().c_str();
            return error;
        }
    }
    return native::last_error();
}

const Class* find_class(const char* image, const char* namespc, const char* name) {
    const auto* value = api();
    if (!available() || !value->find_class)
        return nullptr;
    const auto* klass =
        static_cast<const Class*>(value->find_class(image, namespc, name));
    if (klass && value->find_image)
        remember_class_image(
            klass, static_cast<const Image*>(value->find_image(image)));
    return klass;
}

const Method* resolve_method(const Class* klass, const char* name, int argc) {
    if (!klass || !name)
        return nullptr;
    std::unordered_set<const Method*> visited;
    for (const Class* current = klass; current; current = class_get_parent(current)) {
        void* iterator = nullptr;
        while (const Method* method = class_get_methods(current, &iterator)) {
            if (!visited.insert(method).second)
                break;
            const char* candidate = method_get_name(method);
            if (candidate && std::strcmp(candidate, name) == 0 &&
                (argc < 0 || static_cast<int>(method_get_param_count(method)) == argc))
                return method;
        }
    }
    return nullptr;
}

const Method* resolve_method_exact(const Class* klass, const char* name,
                                   const char* const* parameter_types,
                                   int parameter_count) {
    if (!klass || !name || parameter_count < 0 ||
        (parameter_count > 0 && !parameter_types))
        return nullptr;
    for (const Class* current = klass; current; current = class_get_parent(current)) {
        void* iterator = nullptr;
        while (const Method* method = class_get_methods(current, &iterator)) {
            const char* candidate = method_get_name(method);
            if (!candidate || std::strcmp(candidate, name) != 0 ||
                method_get_param_count(method) != static_cast<std::uint32_t>(parameter_count))
                continue;
            bool exact = true;
            for (int index = 0; index < parameter_count; ++index) {
                if (!same_type_name(method_get_param(method, static_cast<std::uint32_t>(index)),
                                    parameter_types[index])) {
                    exact = false;
                    break;
                }
            }
            if (exact)
                return method;
        }
    }
    return nullptr;
}

const Field* resolve_field(const Class* klass, const char* name) {
    if (!klass || !name)
        return nullptr;
    for (const Class* current = klass; current; current = class_get_parent(current)) {
        void* iterator = nullptr;
        while (const Field* field = class_get_fields(current, &iterator)) {
            const char* candidate = field_get_name(field);
            if (candidate && std::strcmp(candidate, name) == 0)
                return field;
        }
    }
    return nullptr;
}

const Field* find_field(const Class* klass, const char* name) {
    return resolve_field(klass, name);
}

void* method_pointer(const Method* method) {
    const auto* value = api();
    return available() && value->compile_method ? value->compile_method(method) : nullptr;
}

const Domain* domain_get() {
    const auto* value = api();
    return available() && value->domain_get
        ? static_cast<const Domain*>(value->domain_get())
        : nullptr;
}

std::size_t domain_get_assembly_count() {
    return available() ? native::refresh_assemblies() : 0;
}

const Assembly* domain_get_assembly(std::size_t index) {
    return available()
        ? static_cast<const Assembly*>(native::assembly_at(index))
        : nullptr;
}

const Image* assembly_get_image(const Assembly* assembly) {
    const auto* value = api();
    return available() && value->assembly_get_image
        ? static_cast<const Image*>(value->assembly_get_image(assembly))
        : nullptr;
}

const char* image_get_name(const Image* image) {
    const auto* value = api();
    return available() && value->image_get_name ? value->image_get_name(image) : nullptr;
}

std::size_t image_get_class_count(const Image* image) {
    const auto* value = api();
    if (!available() || !image || !value->image_get_table_rows)
        return 0;
    constexpr int mono_table_typedef = 2;
    const int rows = value->image_get_table_rows(image, mono_table_typedef);
    return rows > 0 ? static_cast<std::size_t>(rows) : 0;
}

const Class* image_get_class(const Image* image, std::size_t index) {
    const auto* value = api();
    if (!available() || !image || !value->image_get_class ||
        index >= image_get_class_count(image) ||
        index >= 0x00ffffffu) {
        return nullptr;
    }
    constexpr std::uint32_t mono_token_typedef = 0x02000000u;
    const auto* klass = static_cast<const Class*>(
        value->image_get_class(
            image, mono_token_typedef | (static_cast<std::uint32_t>(index) + 1u)));
    remember_class_image(klass, image);
    return klass;
}

const char* class_get_name(const Class* klass) {
    const auto* value = api();
    return available() && value->class_get_name ? value->class_get_name(klass) : nullptr;
}

const char* class_get_namespace(const Class* klass) {
    const auto* value = api();
    return available() && value->class_get_namespace
        ? value->class_get_namespace(klass)
        : nullptr;
}

const Class* class_get_parent(const Class* klass) {
    const auto* value = api();
    return available() && value->class_get_parent
        ? static_cast<const Class*>(value->class_get_parent(klass))
        : nullptr;
}

const Image* class_get_image(const Class* klass) {
    if (!available() || !klass)
        return nullptr;
    if (const auto* image =
            static_cast<const Image*>(native::class_get_image(klass))) {
        remember_class_image(klass, image);
        return image;
    }
    if (const Image* image = remembered_class_image(klass)) {
        native::clear_error();
        return image;
    }
    return nullptr;
}

const char* class_get_assemblyname(const Class* klass) {
    const Image* image = class_get_image(klass);
    return image ? image_get_name(image) : nullptr;
}

std::uint32_t class_get_flags(const Class* klass) {
    const auto* value = api();
    return available() && value->class_get_flags ? value->class_get_flags(klass) : 0;
}

bool class_is_valuetype(const Class* klass) {
    const auto* value = api();
    return available() && value->class_is_valuetype && value->class_is_valuetype(klass) != 0;
}

bool class_is_enum(const Class* klass) {
    const auto* value = api();
    return available() && value->class_is_enum && value->class_is_enum(klass) != 0;
}

const Field* class_get_fields(const Class* klass, void** iterator) {
    const auto* value = api();
    return available() && value->class_get_fields
        ? static_cast<const Field*>(value->class_get_fields(klass, iterator))
        : nullptr;
}

const Method* class_get_methods(const Class* klass, void** iterator) {
    const auto* value = api();
    return available() && value->class_get_methods
        ? static_cast<const Method*>(value->class_get_methods(klass, iterator))
        : nullptr;
}

const Property* class_get_properties(const Class* klass, void** iterator) {
    const auto* value = api();
    return available() && value->class_get_properties
        ? static_cast<const Property*>(value->class_get_properties(klass, iterator))
        : nullptr;
}

const Class* class_get_interfaces(const Class* klass, void** iterator) {
    const auto* value = api();
    return available() && value->class_get_interfaces
        ? static_cast<const Class*>(value->class_get_interfaces(klass, iterator))
        : nullptr;
}

const Type* class_get_type(const Class* klass) {
    const auto* value = api();
    return available() && value->class_get_type
        ? static_cast<const Type*>(value->class_get_type(klass))
        : nullptr;
}

const Class* class_get_element_class(const Class* klass) {
    return available()
        ? static_cast<const Class*>(native::class_get_element_class(klass))
        : nullptr;
}

std::int32_t class_value_size(const void* klass, std::uint32_t* alignment) {
    return available() ? native::class_value_size(klass, alignment) : -1;
}

int class_is_assignable_from(const void* target, const void* candidate) {
    if (!available() || !target || !candidate)
        return 0;
    if (native::class_is_assignable_from(target, candidate) != 0)
        return 1;
    if (class_has_parent(candidate, target) != 0) {
        native::clear_error();
        return 1;
    }
    return 0;
}

int class_has_parent(const void* klass, const void* parent) {
    std::unordered_set<const void*> visited;
    for (const auto* current = static_cast<const Class*>(klass); current;
         current = class_get_parent(current)) {
        if (current == parent)
            return 1;
        if (!visited.insert(current).second)
            break;
    }
    return 0;
}

const Type* class_enum_basetype(const Class* klass) {
    return available()
        ? static_cast<const Type*>(native::class_enum_basetype(klass))
        : nullptr;
}

const char* method_get_name(const Method* method) {
    const auto* value = api();
    return available() && value->method_get_name ? value->method_get_name(method) : nullptr;
}

std::uint32_t method_get_param_count(const Method* method) {
    const auto* value = api();
    if (!available() || !value->method_signature || !value->signature_get_param_count)
        return 0;
    const void* signature = value->method_signature(method);
    return signature ? value->signature_get_param_count(signature) : 0;
}

const Type* method_get_param(const Method* method, std::uint32_t index) {
    const auto* value = api();
    return available() && value->method_get_param_type
        ? static_cast<const Type*>(value->method_get_param_type(method, index))
        : nullptr;
}

const char* method_get_param_name(const Method* method, std::uint32_t index) {
    return available()
        ? native::method_get_param_name(
              method, index, method_get_param_count(method))
        : nullptr;
}

const Type* method_get_return_type(const Method* method) {
    const auto* value = api();
    return available() && value->method_get_return_type
        ? static_cast<const Type*>(value->method_get_return_type(method))
        : nullptr;
}

std::uint32_t method_get_flags(const Method* method, std::uint32_t* implementation_flags) {
    const auto* value = api();
    return available() && value->method_get_flags
        ? value->method_get_flags(method, implementation_flags)
        : 0;
}

void* method_get_object(const Method* method, const Class*) {
    const auto* value = api();
    return available() && value->method_get_object ? value->method_get_object(method) : nullptr;
}

bool method_is_generic(const Method* method) {
    return available() && native::method_is_generic(method);
}

const char* field_get_name(const Field* field) {
    const auto* value = api();
    return available() && value->field_get_name ? value->field_get_name(field) : nullptr;
}

const Type* field_get_type(const Field* field) {
    const auto* value = api();
    return available() && value->field_get_type
        ? static_cast<const Type*>(value->field_get_type(field))
        : nullptr;
}

std::uint32_t field_get_flags(const Field* field) {
    const auto* value = api();
    return available() && value->field_get_flags ? value->field_get_flags(field) : 0;
}

bool field_get_value(Object* object, const Field* field, void* output) {
    const auto* value = api();
    return available() && value->field_get_value &&
           value->field_get_value(object, field, output) != 0;
}

bool field_set_value(Object* object, const Field* field, void* input) {
    const auto* value = api();
    return available() && value->field_set_value &&
           value->field_set_value(object, field, input) != 0;
}

void* field_get_value_object(const void* field, void* object) {
    return available()
        ? native::field_get_value_object(domain_get(), field, object)
        : nullptr;
}

bool field_static_get_value(const Field* field, void* output) {
    const auto* value = api();
    if (!available() || !value->field_static_get_value)
        return false;
    const void* parent = native::field_get_parent(field);
    return parent && value->field_static_get_value(parent, field, output) != 0;
}

bool field_static_set_value(const Field* field, void* input) {
    const auto* value = api();
    if (!available() || !value->field_static_set_value)
        return false;
    const void* parent = native::field_get_parent(field);
    return parent && value->field_static_set_value(parent, field, input) != 0;
}

const char* property_get_name(const Property* property) {
    const auto* value = api();
    return available() && value->property_get_name ? value->property_get_name(property) : nullptr;
}

const Method* property_get_get_method(const Property* property) {
    const auto* value = api();
    return available() && value->property_get_get_method
        ? static_cast<const Method*>(value->property_get_get_method(property))
        : nullptr;
}

const Method* property_get_set_method(const Property* property) {
    const auto* value = api();
    return available() && value->property_get_set_method
        ? static_cast<const Method*>(value->property_get_set_method(property))
        : nullptr;
}

std::uint32_t property_get_flags(const Property* property) {
    const auto* value = api();
    return available() && value->property_get_flags ? value->property_get_flags(property) : 0;
}

bool type_get_name(const Type* type, char* output, std::size_t output_size) {
    const auto* value = api();
    return available() && value->type_get_name &&
           value->type_get_name(type, output, output_size) != 0;
}

std::int32_t type_get_type(const Type* type) {
    const auto* value = api();
    return available() && value->type_get_type ? value->type_get_type(type) : 0;
}

const Class* type_get_class_or_element_class(const Type* type) {
    const auto* value = api();
    return available() && value->type_get_class
        ? static_cast<const Class*>(value->type_get_class(type))
        : nullptr;
}

void* type_get_object(const Type* type) {
    const auto* value = api();
    return available() && value->type_get_object ? value->type_get_object(type) : nullptr;
}

const Class* object_get_class(Object* object) {
    const auto* value = api();
    return available() && value->object_get_class
        ? static_cast<const Class*>(value->object_get_class(object))
        : nullptr;
}

void* object_unbox(Object* object) {
    const auto* value = api();
    return available() && value->object_unbox ? value->object_unbox(object) : nullptr;
}

Object* object_new(const Class* klass) {
    const auto* value = api();
    return available() && value->object_new
        ? static_cast<Object*>(value->object_new(klass))
        : nullptr;
}

void runtime_object_init(Object* object) {
    const auto* value = api();
    if (available() && value->runtime_object_init)
        value->runtime_object_init(object);
}

void* value_box(const Class* klass, void* data) {
    const auto* value = api();
    return available() && value->value_box ? value->value_box(klass, data) : nullptr;
}

String* string_new_len(const char* utf8, std::uint32_t length) {
    const auto* value = api();
    if (!available() || (!utf8 && length != 0))
        return nullptr;
    const char* source = utf8 ? utf8 : "";
    if (void* string = native::string_new_len(domain_get(), source, length))
        return static_cast<String*>(string);
    if (!value->new_string ||
        std::memchr(source, '\0', static_cast<std::size_t>(length)) != nullptr) {
        return nullptr;
    }
    const std::string copy(source, length);
    String* result = static_cast<String*>(value->new_string(copy.c_str()));
    if (result)
        native::clear_error();
    return result;
}

bool string_to_utf8(String* string, char* output, std::size_t output_size) {
    const auto* value = api();
    return available() && value->string_to_utf8 &&
           value->string_to_utf8(string, output, output_size) != 0;
}

std::int32_t string_length(String* string) {
    const auto* value = api();
    if (!available() || !value->string_length)
        return -1;
    const std::size_t length = value->string_length(string);
    return length <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        ? static_cast<std::int32_t>(length)
        : -1;
}

bool has_array_length() {
    const auto* value = api();
    return available() && value->array_length;
}

std::size_t array_length(Array* array) {
    const auto* value = api();
    return has_array_length() ? value->array_length(array) : 0;
}

void* array_addr_with_size(Array* array, int element_size, std::size_t index) {
    const auto* value = api();
    return available() && value->array_address
        ? value->array_address(array, element_size, index)
        : nullptr;
}

bool has_array_ref_at() {
    const auto* value = api();
    return available() && value->array_ref_at;
}

void* array_ref_at(Array* array, std::size_t index) {
    const auto* value = api();
    return has_array_ref_at() ? value->array_ref_at(array, index) : nullptr;
}

bool has_array_set_ref() {
    const auto* value = api();
    return available() && value->array_set_ref;
}

bool array_set_ref(Array* array, std::size_t index, void* item) {
    const auto* value = api();
    return has_array_set_ref() && value->array_set_ref(array, index, item) != 0;
}

int runtime_invoke(const Method* method, Object* object, void** params,
                   void** result, void** exception) {
    const auto* value = api();
    std::uint32_t native_exception = 0;
    invocation_error_slot().clear();
    if (!available() || !value->runtime_invoke)
        return 0;

    const int status = value->runtime_invoke(method, object, params, result,
                                             exception, &native_exception);
    if (native_exception != 0) {
        invocation_error_slot() = "Mono runtime_invoke raised native exception 0x" +
            std::to_string(native_exception);
        return status;
    }
    if (exception && *exception) {
        std::string message = native::exception_message(*exception);
        if (!message.empty())
            invocation_error_slot() = "Mono managed exception: " + message;
    }
    return status;
}

GCHandle gchandle_new(void* object, int pinned) {
    const auto* value = api();
    return available() && value->gchandle_new
        ? static_cast<GCHandle>(value->gchandle_new(object, pinned))
        : 0;
}

GCHandle gchandle_new_weakref(void* object, int track_resurrection) {
    const auto* value = api();
    return available() && value->gchandle_new_weakref
        ? static_cast<GCHandle>(value->gchandle_new_weakref(object, track_resurrection))
        : 0;
}

void* gchandle_get_target(GCHandle handle) {
    const auto* value = api();
    return available() && value->gchandle_get_target
        ? value->gchandle_get_target(static_cast<std::uint32_t>(handle))
        : nullptr;
}

void gchandle_free(GCHandle handle) {
    const auto* value = api();
    if (available() && value->gchandle_free)
        value->gchandle_free(static_cast<std::uint32_t>(handle));
}

std::int64_t gc_get_used_size() {
    return available() ? native::gc_get_used_size() : 0;
}

std::int64_t gc_get_heap_size() {
    return available() ? native::gc_get_heap_size() : 0;
}

} // namespace URK::mono
