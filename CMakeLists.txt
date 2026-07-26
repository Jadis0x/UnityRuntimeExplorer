# Copyright (c) 2026 Jadis0x. All rights reserved.
cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Generated mods keep dependencies linked into the mod DLL when possible" FORCE)

project(URK_UnityRuntimeExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The distributed Explorer mod must run without a separate Visual C++ runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
    "Use the static Microsoft C/C++ runtime" FORCE)

if(NOT WIN32)
    message(FATAL_ERROR "Generated URKit starter mods target Windows native Unity processes.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Generated URKit starter mods support x64 targets only.")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies as shared libraries" FORCE)

function(urk_disable_vs_vcpkg_integration target_name)
    if(MSVC)
        set_target_properties(${target_name} PROPERTIES VS_GLOBAL_VcpkgEnabled "false")
    endif()
endfunction()

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.2b-docking
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
set_target_properties(imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF)
if(MSVC)
    target_compile_options(imgui PRIVATE /W0 /permissive- /EHsc /utf-8)
endif()
urk_disable_vs_vcpkg_integration(imgui)

set(URK_COMMON_SOURCES
        mod/support/mod_log.cpp
        mod/hooks/mod_hooks.cpp
        mod/hooks/render_imgui_hook.cpp
        mod/hooks/unity_log_hook.cpp
        mod/lifecycle/mod_network.cpp
        mod/lifecycle/mod_runtime.cpp
        mod/generated/mod_lifecycle.cpp
        mod/generated/mod_entry.cpp
        mod/explorer/explorer_model.cpp
        mod/explorer/explorer_ui.cpp
        mod/explorer/method_tracer.cpp
        mod/explorer/camera/camera_focus_controller.cpp
        mod/ui/highlight.cpp
)

set(URK_COMMON_HEADERS
        sdk/runtime_api.h
        sdk/runtime_bootstrap.h
        sdk/runtime/managed_runtime.h
        sdk/runtime/managed_hooks.h
        sdk/runtime/managed_strings.h
        sdk/hook_api.h
        sdk/network_api.h
        sdk/events.h
        sdk/coroutines.h
        sdk/mod_async.h
        sdk/unity/unity.h
        sdk/unity/unity_types.h
        sdk/unity/detail/managed_backend.h
        sdk/unity/unity_invoke.h
        sdk/unity/unity_components.h
        sdk/unity/unity_inspect.h
        sdk/unity/unity_shortcuts.h
        mod/ui/theme.h
        mod/ui/localization.h
        mod/ui/widgets.h
        mod/ui/tabs/about_tab.h
        mod/ui/tabs/config_tab.h
        mod/ui/menu.h
        mod/ui/highlight.h
        mod/hooks/render_imgui_hook.h
        mod/hooks/unity_log_hook.h
        mod/generated/mod_lifecycle.h
        mod/explorer/explorer_types.h
        mod/explorer/explorer_commands.h
        mod/explorer/explorer_model.h
        mod/explorer/explorer_ui.h
        mod/explorer/method_tracer.h
        mod/explorer/camera/camera_focus_controller.h
        mod/hooks/mod_hooks.h
        mod/lifecycle/mod_network.h
        mod/lifecycle/mod_runtime.h
        mod/support/mod_log.h
        mod/config/mod_config.h
)

set(URK_IL2CPP_BACKEND_SOURCES
    sdk/il2cpp/il2cpp_runtime.h
    sdk/il2cpp/il2cpp_helpers.h
)

set(URK_MONO_BACKEND_SOURCES
    sdk/mono/mono_runtime.h
    sdk/mono/mono_runtime.cpp
)

function(urk_add_explorer_target target_name backend_definition)
    add_library(${target_name} SHARED)
    set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_include_directories(${target_name} PRIVATE . mod)
    target_sources(${target_name}
        PRIVATE
            ${URK_COMMON_SOURCES}
            ${URK_COMMON_HEADERS}
            ${ARGN}
    )
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        ${URK_COMMON_SOURCES}
        ${URK_COMMON_HEADERS}
        ${ARGN}
    )
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_definitions(${target_name} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        ${backend_definition}
    )
    target_link_libraries(${target_name} PRIVATE imgui d3d11 d3d12 dxgi opengl32 ntdll)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc /utf-8 /bigobj)
        target_link_options(${target_name} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
        )
    endif()
    urk_disable_vs_vcpkg_integration(${target_name})
endfunction()

urk_add_explorer_target(
    URK_Il2cpp_UnityRuntimeExplorer
    URK_BACKEND_IL2CPP
    ${URK_IL2CPP_BACKEND_SOURCES}
)

urk_add_explorer_target(
    URK_Mono_UnityRuntimeExplorer
    URK_BACKEND_MONO
    ${URK_MONO_BACKEND_SOURCES}
)

set(URK_DEPLOY_DIR "" CACHE PATH "Optional game Mods directory where the built DLL will be copied")
if(URK_DEPLOY_DIR)
    foreach(target_name IN ITEMS URK_Il2cpp_UnityRuntimeExplorer URK_Mono_UnityRuntimeExplorer)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${URK_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${target_name}> "${URK_DEPLOY_DIR}"
        )
    endforeach()
endif()
