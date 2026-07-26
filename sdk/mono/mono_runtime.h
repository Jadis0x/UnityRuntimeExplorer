#pragma once

#include "../runtime_api.h"

#include <cstddef>
#include <cstdint>

namespace URK::mono {

using Domain = void;
using Assembly = void;
using Image = void;
using Class = void;
using Method = void;
using Field = void;
using Property = void;
using Type = void;
using Object = void;
using String = void;
using Array = void;
using Thread = void;
using GCHandle = std::uintptr_t;

bool init(const URK::ModContext* context);
const URK_MonoApi* api();
bool available();
const char* last_error();

const Class* find_class(const char* image, const char* namespc, const char* name);
const Method* resolve_method(const Class* klass, const char* name, int argc);
const Method* resolve_method_exact(const Class* klass, const char* name,
                                   const char* const* parameter_types, int parameter_count);
const Field* resolve_field(const Class* klass, const char* name);
const Field* find_field(const Class* klass, const char* name);
void* method_pointer(const Method* method);

const Domain* domain_get();
std::size_t domain_get_assembly_count();
const Assembly* domain_get_assembly(std::size_t index);
const Image* assembly_get_image(const Assembly* assembly);
const char* image_get_name(const Image* image);
std::size_t image_get_class_count(const Image* image);
const Class* image_get_class(const Image* image, std::size_t index);

const char* class_get_name(const Class* klass);
const char* class_get_namespace(const Class* klass);
const Class* class_get_parent(const Class* klass);
const Image* class_get_image(const Class* klass);
const char* class_get_assemblyname(const Class* klass);
std::uint32_t class_get_flags(const Class* klass);
bool class_is_valuetype(const Class* klass);
bool class_is_enum(const Class* klass);
const Field* class_get_fields(const Class* klass, void** iterator);
const Method* class_get_methods(const Class* klass, void** iterator);
const Property* class_get_properties(const Class* klass, void** iterator);
const Class* class_get_interfaces(const Class* klass, void** iterator);
const Type* class_get_type(const Class* klass);
const Class* class_get_element_class(const Class* klass);
std::int32_t class_value_size(const void* klass, std::uint32_t* alignment);
int class_is_assignable_from(const void* target, const void* candidate);
int class_has_parent(const void* klass, const void* parent);
const Type* class_enum_basetype(const Class* klass);

const char* method_get_name(const Method* method);
std::uint32_t method_get_param_count(const Method* method);
const Type* method_get_param(const Method* method, std::uint32_t index);
const char* method_get_param_name(const Method* method, std::uint32_t index);
const Type* method_get_return_type(const Method* method);
std::uint32_t method_get_flags(const Method* method, std::uint32_t* implementation_flags);
void* method_get_object(const Method* method, const Class* reference_class);
bool method_is_generic(const Method* method);

const char* field_get_name(const Field* field);
const Type* field_get_type(const Field* field);
std::uint32_t field_get_flags(const Field* field);
bool field_get_value(Object* object, const Field* field, void* output);
bool field_set_value(Object* object, const Field* field, void* value);
void* field_get_value_object(const void* field, void* object);
bool field_static_get_value(const Field* field, void* output);
bool field_static_set_value(const Field* field, void* value);

const char* property_get_name(const Property* property);
const Method* property_get_get_method(const Property* property);
const Method* property_get_set_method(const Property* property);
std::uint32_t property_get_flags(const Property* property);

bool type_get_name(const Type* type, char* output, std::size_t output_size);
std::int32_t type_get_type(const Type* type);
const Class* type_get_class_or_element_class(const Type* type);
void* type_get_object(const Type* type);

const Class* object_get_class(Object* object);
void* object_unbox(Object* object);
Object* object_new(const Class* klass);
void runtime_object_init(Object* object);
void* value_box(const Class* klass, void* data);

String* string_new_len(const char* utf8, std::uint32_t length);
bool string_to_utf8(String* string, char* output, std::size_t output_size);
std::int32_t string_length(String* string);

bool has_array_length();
std::size_t array_length(Array* array);
void* array_addr_with_size(Array* array, int element_size, std::size_t index);
bool has_array_ref_at();
void* array_ref_at(Array* array, std::size_t index);
bool has_array_set_ref();
bool array_set_ref(Array* array, std::size_t index, void* value);

int runtime_invoke(const Method* method, Object* object, void** params,
                   void** result, void** exception);

GCHandle gchandle_new(void* object, int pinned);
GCHandle gchandle_new_weakref(void* object, int track_resurrection);
void* gchandle_get_target(GCHandle handle);
void gchandle_free(GCHandle handle);

std::int64_t gc_get_used_size();
std::int64_t gc_get_heap_size();

} // namespace URK::mono
