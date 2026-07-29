#pragma once

#include "../runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace URK::mono::native {

// Resolves the Mono embedding exports that are not part of the generated
// URK Mono v7 table. The module is loader-owned; this adapter never unloads it.
void initialize(const URK::ModContext* context);
void reset();
bool available();
const char* last_error();
void clear_error();

std::size_t refresh_assemblies();
const void* assembly_at(std::size_t index);

const void* class_get_image(const void* klass);
const void* class_get_element_class(const void* klass);
std::int32_t class_value_size(const void* klass, std::uint32_t* alignment);
int class_is_assignable_from(const void* target, const void* candidate);
const void* class_enum_basetype(const void* klass);

bool method_is_generic(const void* method);
const char* method_get_param_name(const void* method, std::uint32_t index,
                                  std::uint32_t parameter_count);

const void* field_get_parent(const void* field);
void* field_get_value_object(const void* domain, const void* field, void* object);

void* string_new_len(const void* domain, const char* utf8, std::uint32_t length);
std::string exception_message(void* exception);
std::int64_t gc_get_used_size();
std::int64_t gc_get_heap_size();

} // namespace URK::mono::native
