#include "mono_runtime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace URK::mono {
namespace {

const URK_MonoApi*& api_slot() {
    static const URK_MonoApi* value = nullptr;
    return value;
}

bool has_field(const URK_MonoApi* value, std::size_t end) {
    return value && value->version >= URK_MONO_API_VERSION && value->size >= end;
}

#define URK_MONO_HAS(member) \
    (has_field(value, offsetof(URK_MonoApi, member) + sizeof(value->member)) && value->member)

bool same_type_name(const Type* type, const char* requested) {
    if (!type || !requested)
        return false;
    char name[512]{};
    return type_get_name(type, name, sizeof(name)) && std::string_view{name} == requested;
}

} // namespace

bool init(const URK::ModContext* context) {
    URK::set_context(context);
    api_slot() = nullptr;
    if (!context || context->version < URK_SDK_VERSION ||
        context->size < sizeof(URK_ModContext) ||
        context->runtimeBackend != URK::runtime_backend_mono ||
        !URK::has_runtime_capability(URK::runtime_cap_mono_api) ||
        !context->mono || context->mono->version < URK_MONO_API_VERSION ||
        context->mono->size < sizeof(URK_MonoApi))
        return false;

    api_slot() = context->mono;
    const auto* value = api_slot();
    if (!URK_MONO_HAS(is_available) || value->is_available() == 0) {
        api_slot() = nullptr;
        return false;
    }
    return !value->attach_current_thread || value->attach_current_thread() != 0;
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
    return URK_MONO_HAS(is_available) && value->is_available() != 0;
}

const char* last_error() {
    const auto* value = api();
    return value && value->last_error ? value->last_error() : nullptr;
}

const Class* find_class(const char* image, const char* namespc, const char* name) {
    const auto* value = api();
    return available() && value->find_class
        ? static_cast<const Class*>(value->find_class(image, namespc, name))
        : nullptr;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(domain_get_assembly_count)
        ? value->domain_get_assembly_count()
        : 0;
}

const Assembly* domain_get_assembly(std::size_t index) {
    const auto* value = api();
    return available() && URK_MONO_HAS(domain_get_assembly)
        ? static_cast<const Assembly*>(value->domain_get_assembly(index))
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
    return available() && URK_MONO_HAS(image_get_class_count)
        ? value->image_get_class_count(image)
        : 0;
}

const Class* image_get_class(const Image* image, std::size_t index) {
    const auto* value = api();
    return available() && URK_MONO_HAS(image_get_class_at)
        ? static_cast<const Class*>(value->image_get_class_at(image, index))
        : nullptr;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(class_get_image)
        ? static_cast<const Image*>(value->class_get_image(klass))
        : nullptr;
}

const char* class_get_assemblyname(const Class* klass) {
    const auto* value = api();
    return available() && URK_MONO_HAS(class_get_assemblyname)
        ? value->class_get_assemblyname(klass)
        : nullptr;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(class_get_element_class)
        ? static_cast<const Class*>(value->class_get_element_class(klass))
        : nullptr;
}

std::int32_t class_value_size(const void* klass, std::uint32_t* alignment) {
    const auto* value = api();
    return available() && URK_MONO_HAS(class_value_size)
        ? value->class_value_size(klass, alignment)
        : -1;
}

int class_is_assignable_from(const void* target, const void* candidate) {
    const auto* value = api();
    return available() && URK_MONO_HAS(class_is_assignable_from)
        ? value->class_is_assignable_from(target, candidate)
        : 0;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(class_enum_basetype)
        ? static_cast<const Type*>(value->class_enum_basetype(klass))
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
    const auto* value = api();
    return available() && URK_MONO_HAS(method_get_param_name)
        ? value->method_get_param_name(method, index)
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
    const auto* value = api();
    return available() && value->method_is_generic && value->method_is_generic(method) != 0;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(field_get_value_object)
        ? value->field_get_value_object(field, object)
        : nullptr;
}

bool field_static_get_value(const Field* field, void* output) {
    const auto* value = api();
    if (!available() || !value->field_static_get_value || !URK_MONO_HAS(field_get_parent))
        return false;
    const void* parent = value->field_get_parent(field);
    return parent && value->field_static_get_value(parent, field, output) != 0;
}

bool field_static_set_value(const Field* field, void* input) {
    const auto* value = api();
    if (!available() || !value->field_static_set_value || !URK_MONO_HAS(field_get_parent))
        return false;
    const void* parent = value->field_get_parent(field);
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
    return available() && URK_MONO_HAS(string_new_len)
        ? static_cast<String*>(value->string_new_len(utf8, length))
        : nullptr;
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
    return available() && value->runtime_invoke
        ? value->runtime_invoke(method, object, params, result, exception, &native_exception)
        : 0;
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
    const auto* value = api();
    return available() && URK_MONO_HAS(gc_get_used_size) ? value->gc_get_used_size() : 0;
}

std::int64_t gc_get_heap_size() {
    const auto* value = api();
    return available() && URK_MONO_HAS(gc_get_heap_size) ? value->gc_get_heap_size() : 0;
}

#undef URK_MONO_HAS
} // namespace URK::mono
