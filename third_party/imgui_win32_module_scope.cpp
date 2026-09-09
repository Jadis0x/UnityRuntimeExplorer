// Dear ImGui's Win32 backend, compiled so its viewport window class belongs to
// this mod instead of to the game executable.
//
// imgui_impl_win32.cpp registers the "ImGui Platform" class, creates every
// secondary viewport window, and unregisters the class again with
// GetModuleHandle(nullptr) -- the executable. A window class atom is keyed on
// (name, hInstance), so two injected mods that both enable multi-viewport
// support contend for a single atom:
//
//   * the second RegisterClassExW() fails with ERROR_CLASS_ALREADY_EXISTS,
//     which the backend does not check;
//   * its viewport windows resolve to the first mod's class and are dispatched
//     by the first mod's window procedure, which then drives the first mod's
//     copy of Dear ImGui with a context that belongs to the second -- only the
//     two builds happening to be layout-identical keeps that from corrupting
//     memory;
//   * and whichever mod shuts down first unregisters the class out from under
//     the other, so its later viewports cannot be created at all.
//
// Naming this DLL in those three calls gives every mod its own atom, its own
// window procedure and its own unregistration. The backend is compiled from
// here rather than straight from the upstream source because the module handle
// is hard-coded at each of the three call sites; nothing else about it changes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {
char urk_imgui_module_anchor = 0;
} // namespace

// Not in an anonymous namespace: the backend calls this through the macro below
// as ::GetModuleHandle(nullptr), which requires a global-scope name.
static HMODULE urk_imgui_viewport_class_module(decltype(nullptr)) {
    static HMODULE self = [] {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&urk_imgui_module_anchor), &module);
        // A null handle would send the backend back to the executable, which is
        // the collision this file exists to avoid; there is nothing better to
        // fall back to, so let the original behaviour stand rather than fail.
        return module;
    }();
    return self;
}

// Overloads for a named module keep any future call site behaving exactly as
// the Win32 function it replaces.
static HMODULE urk_imgui_viewport_class_module(const wchar_t *name) {
    return GetModuleHandleW(name);
}

static HMODULE urk_imgui_viewport_class_module(const char *name) {
    return GetModuleHandleA(name);
}

#undef GetModuleHandle
#define GetModuleHandle(name) urk_imgui_viewport_class_module(name)

#include <backends/imgui_impl_win32.cpp>

#undef GetModuleHandle
