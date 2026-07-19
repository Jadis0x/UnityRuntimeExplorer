// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "config/mod_config.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <vector>
#include <windowsx.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 5202)
#endif
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "sdk/runtime_api.h"
#include "sdk/hook_api.h"
#include "ui/menu.h"
#include "ui/highlight.h"
#include "ui/theme.h"

namespace ModRenderHook {
using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using WglSwapBuffersFn = BOOL(WINAPI*)(HDC);

enum class GraphicsBackend {
  none,
  dx11,
  dx12,
  opengl,
};

struct DxgiVTableTargets {
  void* present{};
  void* present1{};
  void* resize_buffers{};
};

enum class InputEventKind {
  mouse_position,
  mouse_button,
  mouse_wheel,
  focus,
  raw_message,
  release_all_mouse,
  release_all_input,
};

struct InputEvent {
  InputEventKind kind{};
  HWND hwnd{};
  UINT message{};
  WPARAM wparam{};
  LPARAM lparam{};
  float x{};
  float y{};
  int button{-1};
  bool state{};
};

struct KeyboardButtonState {
  bool down{};
  bool system{};
  LPARAM lparam{};
};

struct MouseButtonState {
  bool down{};
  LPARAM lparam{};
};

inline PresentFn g_present = nullptr;
inline Present1Fn g_present1 = nullptr;
inline ResizeBuffersFn g_resize_buffers = nullptr;
inline ExecuteCommandListsFn g_execute_command_lists = nullptr;
inline WglSwapBuffersFn g_wgl_swap_buffers = nullptr;
inline HWND g_hwnd = nullptr;
inline WNDPROC g_old_wndproc = nullptr;
inline ID3D11Device* g_device = nullptr;
inline ID3D11DeviceContext* g_context = nullptr;
inline ID3D11RenderTargetView* g_render_target = nullptr;
inline IDXGISwapChain* g_active_swap_chain = nullptr;
inline ID3D12Device* g_dx12_device = nullptr;
inline ID3D12CommandQueue* g_dx12_command_queue = nullptr;
inline ID3D12DescriptorHeap* g_dx12_rtv_heap = nullptr;
inline ID3D12DescriptorHeap* g_dx12_srv_heap = nullptr;
inline ID3D12GraphicsCommandList* g_dx12_command_list = nullptr;
inline ID3D12Fence* g_dx12_fence = nullptr;
inline HANDLE g_dx12_fence_event = nullptr;
inline UINT g_dx12_rtv_descriptor_size = 0;
inline UINT g_dx12_srv_descriptor_size = 0;
inline UINT64 g_dx12_next_fence_value = 1;
inline constexpr UINT kDx12SrvDescriptorCapacity = 64;
inline std::array<bool, kDx12SrvDescriptorCapacity> g_dx12_srv_descriptors{};
struct Dx12FrameContext { ID3D12CommandAllocator* allocator{}; ID3D12Resource* resource{}; UINT64 fence_value{}; };
inline std::vector<Dx12FrameContext> g_dx12_frames;
inline DXGI_FORMAT g_dx12_format = DXGI_FORMAT_UNKNOWN;
inline HGLRC g_gl_context = nullptr;
inline GraphicsBackend g_backend = GraphicsBackend::none;
inline bool g_imgui_ready = false;
inline bool g_present_hooked = false;
inline bool g_present1_hooked = false;
inline bool g_resize_hooked = false;
inline bool g_execute_command_lists_hooked = false;
inline bool g_wgl_swap_buffers_hooked = false;
inline std::atomic_bool g_shutting_down{false};
inline std::atomic_uint g_active_frames{0};
inline std::atomic<DWORD> g_render_thread_id{0};
inline thread_local unsigned g_present_depth = 0;
inline std::recursive_mutex g_imgui_mutex;
inline std::mutex g_install_mutex;
inline std::mutex g_input_mutex;
inline std::deque<InputEvent> g_input_events;
inline std::array<bool, 5> g_wndproc_mouse_down{};
inline std::array<KeyboardButtonState, 256> g_physical_keyboard{};
inline std::array<KeyboardButtonState, 256> g_game_keyboard{};
inline std::array<bool, 256> g_menu_released_keyboard{};
inline std::array<MouseButtonState, 5> g_physical_mouse{};
inline std::array<MouseButtonState, 5> g_game_mouse{};
inline std::array<bool, 5> g_menu_released_mouse{};
inline std::atomic_uint g_menu_toggle_count{0};
inline std::atomic_bool g_menu_visible{false};
inline std::atomic_int g_menu_input_requested{-1};
inline bool g_menu_input_open = false;
inline bool g_game_focus_suspended = false;
inline std::atomic_bool g_cursor_lease{false};
inline std::atomic_bool g_native_cursor_owned{false};
inline std::atomic_int g_native_cursor_requested{0};
inline RECT g_saved_cursor_clip{};
inline HCURSOR g_saved_cursor = nullptr;
inline int g_native_cursor_show_balance = 0;
inline POINT g_virtual_mouse_position{};
inline bool g_virtual_mouse_position_valid = false;
inline bool g_raw_mouse_seen = false;
inline DXGI_FORMAT g_back_buffer_format = DXGI_FORMAT_UNKNOWN;
inline bool g_linear_color_space_support_known = false;
inline bool g_linear_color_space_supported = false;
inline bool g_render_target_diagnostics_logged = false;
inline bool g_dx12_queue_wait_logged = false;
inline const URK::ModContext* g_mod_context = nullptr;
inline bool g_install_callback_registered = false;
inline bool g_install_complete = false;
inline bool g_install_failed = false;
inline std::uint32_t g_graphics_probe_attempts = 0;
inline constexpr std::uint32_t kMaxGraphicsProbeAttempts = 120;
inline DxgiVTableTargets g_cached_dxgi_targets{};
inline bool g_dxgi_targets_discovered = false;

struct InputRelay {
  UINT message{};
  WPARAM wparam{};
  LPARAM lparam{};
};

struct RenderFrameGuard {
  bool active = false;
  RenderFrameGuard() {
    if (g_shutting_down.load(std::memory_order_acquire)) return;
    g_active_frames.fetch_add(1, std::memory_order_acq_rel);
    if (g_shutting_down.load(std::memory_order_acquire)) {
      g_active_frames.fetch_sub(1, std::memory_order_acq_rel);
      return;
    }
    active = true;
    g_render_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
  }
  ~RenderFrameGuard() {
    if (active) g_active_frames.fetch_sub(1, std::memory_order_acq_rel);
  }
};

inline LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
inline void install_on_main_thread();

inline void log(const char* text) {
  const URK::ModContext* ctx = URK::context();
  if (ctx && ctx->Log) ctx->Log("[%s][ui] %s", ModConfig::display_name, text ? text : "");
}

[[noreturn]] inline void fail_dx12_srv_descriptor(const char* reason) {
  log(reason);
  std::terminate();
}

inline void dx12_allocate_srv_descriptor(ImGui_ImplDX12_InitInfo*,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle,
                                         D3D12_GPU_DESCRIPTOR_HANDLE* gpu_handle) {
  if (!cpu_handle || !gpu_handle || !g_dx12_srv_heap || !g_dx12_srv_descriptor_size)
    fail_dx12_srv_descriptor("DX12 ImGui SRV descriptor allocation received an invalid heap or handle.");

  for (UINT index = 0; index < kDx12SrvDescriptorCapacity; ++index) {
    if (g_dx12_srv_descriptors[index]) continue;
    g_dx12_srv_descriptors[index] = true;
    *cpu_handle = g_dx12_srv_heap->GetCPUDescriptorHandleForHeapStart();
    *gpu_handle = g_dx12_srv_heap->GetGPUDescriptorHandleForHeapStart();
    cpu_handle->ptr += static_cast<SIZE_T>(index) * g_dx12_srv_descriptor_size;
    gpu_handle->ptr += static_cast<UINT64>(index) * g_dx12_srv_descriptor_size;
    return;
  }
  fail_dx12_srv_descriptor("DX12 ImGui exhausted its 64-entry shader-visible SRV descriptor heap.");
}

inline void dx12_free_srv_descriptor(ImGui_ImplDX12_InitInfo*,
                                     D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                     D3D12_GPU_DESCRIPTOR_HANDLE) {
  if (!g_dx12_srv_heap || !g_dx12_srv_descriptor_size)
    fail_dx12_srv_descriptor("DX12 ImGui SRV descriptor release received an invalid heap.");

  const SIZE_T first = g_dx12_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
  if (cpu_handle.ptr < first) fail_dx12_srv_descriptor("DX12 ImGui attempted to release an invalid SRV descriptor.");
  const SIZE_T offset = cpu_handle.ptr - first;
  if (offset % g_dx12_srv_descriptor_size != 0)
    fail_dx12_srv_descriptor("DX12 ImGui attempted to release an unaligned SRV descriptor.");
  const SIZE_T index = offset / g_dx12_srv_descriptor_size;
  if (index >= kDx12SrvDescriptorCapacity || !g_dx12_srv_descriptors[index])
    fail_dx12_srv_descriptor("DX12 ImGui attempted to release an unknown SRV descriptor.");
  g_dx12_srv_descriptors[index] = false;
}

inline UINT input_relay_message() {
  static const UINT message = RegisterWindowMessageW(
      L"UnityRuntimeKit.GeneratedImGui.InputRelay.v1");
  static const bool failure_logged = [&] {
    if (!message) log("RegisterWindowMessage failed for the ImGui input relay.");
    return !message;
  }();
  (void)failure_logged;
  return message;
}

inline UINT menu_cursor_message() {
  static const UINT message = [] {
    char name[256]{};
    const int length = std::snprintf(
        name, sizeof(name), "UnityRuntimeKit.GeneratedImGui.Cursor.%s",
        ModConfig::mod_id ? ModConfig::mod_id : "");
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name)) {
      log("The mod id is too long to register the ImGui cursor message.");
      return UINT{};
    }
    const UINT registered = RegisterWindowMessageA(name);
    if (!registered)
      log("RegisterWindowMessage failed for the ImGui cursor state.");
    return registered;
  }();
  return message;
}

inline UINT menu_input_message() {
  static const UINT message = [] {
    char name[256]{};
    const int length = std::snprintf(
        name, sizeof(name), "UnityRuntimeKit.GeneratedImGui.Input.%s",
        ModConfig::mod_id ? ModConfig::mod_id : "");
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name)) {
      log("The mod id is too long to register the ImGui input state message.");
      return UINT{};
    }
    const UINT registered = RegisterWindowMessageA(name);
    if (!registered)
      log("RegisterWindowMessage failed for the ImGui input state.");
    return registered;
  }();
  return message;
}

inline bool readable_range(const void* ptr, std::size_t bytes) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!ptr || bytes == 0 || VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi) ||
      mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
  const auto address = reinterpret_cast<std::uintptr_t>(ptr);
  return address >= base && bytes <= (base + mbi.RegionSize) - address;
}

inline bool readable(const void* ptr) {
  return readable_range(ptr, 1);
}

inline bool query_swap_chain_desc(IDXGISwapChain* swap_chain, DXGI_SWAP_CHAIN_DESC* desc) {
  if (!swap_chain || !desc) return false;
  std::memset(desc, 0, sizeof(*desc));
  return SUCCEEDED(swap_chain->GetDesc(desc));
}

inline bool is_process_main_window(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd ||
      GetWindow(hwnd, GW_OWNER) != nullptr) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(hwnd, &process_id);
  return process_id == GetCurrentProcessId();
}

inline bool same_com_identity(IUnknown* left, IUnknown* right) {
  if (!left || !right) return false;
  IUnknown* left_identity = nullptr;
  IUnknown* right_identity = nullptr;
  const bool ready =
      SUCCEEDED(left->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&left_identity))) &&
      SUCCEEDED(right->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&right_identity))) &&
      left_identity && right_identity;
  const bool same = ready && left_identity == right_identity;
  if (left_identity) left_identity->Release();
  if (right_identity) right_identity->Release();
  return same;
}

inline bool executable(const void* ptr) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!ptr || VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) {
    return false;
  }
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
    return false;
  const DWORD protection = mbi.Protect & 0xff;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

inline bool valid_hook_target(const void* ptr) {
  return executable(ptr);
}

inline bool is_srgb_format(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

inline bool is_linear_back_buffer_format(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
  case DXGI_FORMAT_R11G11B10_FLOAT:
    return true;
  default:
    return false;
  }
}

inline const char* dxgi_format_name(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
  case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
  case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
  case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return "B8G8R8X8_UNORM_SRGB";
  case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
  case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
  case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
  case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
  default: return "other";
  }
}

inline void query_swap_chain_color_space(IDXGISwapChain* swap_chain) {
  g_linear_color_space_support_known = false;
  g_linear_color_space_supported = false;
  IDXGISwapChain3* swap_chain3 = nullptr;
  if (swap_chain && SUCCEEDED(swap_chain->QueryInterface(__uuidof(IDXGISwapChain3),
      reinterpret_cast<void**>(&swap_chain3))) && swap_chain3) {
    UINT support = 0;
    if (SUCCEEDED(swap_chain3->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support))) {
      g_linear_color_space_support_known = true;
      g_linear_color_space_supported = (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
    }
    swap_chain3->Release();
  }
}

inline bool color_space_needs_linear_ui() {
  return false;
}

inline bool render_target_needs_linear_ui(DXGI_FORMAT swap_chain_format) {
  return is_srgb_format(swap_chain_format) ||
         is_srgb_format(g_back_buffer_format) ||
         is_linear_back_buffer_format(swap_chain_format) ||
         is_linear_back_buffer_format(g_back_buffer_format) ||
         color_space_needs_linear_ui();
}

inline void log_render_target_diagnostics(DXGI_FORMAT swap_chain_format) {
  if (g_render_target_diagnostics_logged) return;
  g_render_target_diagnostics_logged = true;

  char text[512]{};
  std::snprintf(text, sizeof(text),
      "DX11 swap chain format=%s backBuffer=%s linearColorSpaceSupport=%s linearUi=%s.",
      dxgi_format_name(swap_chain_format),
      dxgi_format_name(g_back_buffer_format),
      g_linear_color_space_support_known ? (g_linear_color_space_supported ? "yes" : "no") : "unavailable",
      render_target_needs_linear_ui(swap_chain_format) ? "yes" : "no");
  log(text);
}

inline float srgb_to_linear(float value) {
  if (value <= 0.04045f) return value / 12.92f;
  return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline void linearize_color(ImVec4& color) {
  color.x = srgb_to_linear(color.x);
  color.y = srgb_to_linear(color.y);
  color.z = srgb_to_linear(color.z);
}

inline void apply_swap_chain_color_space(DXGI_FORMAT format) {
  static bool applied = false;
  const bool needs_linear_ui = render_target_needs_linear_ui(format);
  if (!needs_linear_ui || applied) return;
  applied = true;

  ModUI::Theme::Palette& p = ModUI::Theme::palette();
  linearize_color(p.bg_base);
  linearize_color(p.bg_overlay);
  linearize_color(p.bg_elevated);
  linearize_color(p.surface);
  linearize_color(p.surface_raised);
  linearize_color(p.surface_hover);
  linearize_color(p.surface_active);
  linearize_color(p.surface_glass);
  linearize_color(p.border_subtle);
  linearize_color(p.border_strong);
  linearize_color(p.border_focus);
  linearize_color(p.text_primary);
  linearize_color(p.text_secondary);
  linearize_color(p.text_muted);
  linearize_color(p.text_disabled);
  linearize_color(p.accent_a);
  linearize_color(p.accent_b);
  linearize_color(p.accent_c);
  linearize_color(p.accent_soft);
  linearize_color(p.accent_line);
  linearize_color(p.accent_warm);
  linearize_color(p.success);
  linearize_color(p.warning);
  linearize_color(p.danger);
  linearize_color(p.info);
  linearize_color(p.shadow);
  log("DX11 render target expects linear UI colors; UI palette converted to linear.");
}

inline void log_font_diagnostics(const char* renderer_name, const char* sampler_filter) {
  static bool logged = false;
  if (logged) return;
  logged = true;

  const float font_size = ImGui::GetFontSize();
  char text[512]{};
  std::snprintf(text, sizeof(text),
      "%s font source=%s (%s) CJK=%s size=%.1fpx oversample=3x2 sampler=%s.",
      renderer_name ? renderer_name : "ImGui",
      ModUI::Theme::loaded_font_name(),
      ModUI::Theme::using_ttf_font() ? "TTF" : "ImGui default",
      ModUI::Theme::cjk_font_support(),
      font_size,
      sampler_filter ? sampler_filter : "linear");
  log(text);
}

inline HWND find_main_window() {
  struct SearchState { DWORD pid; HWND hwnd; } state{GetCurrentProcessId(), nullptr};
  EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
    auto* state = reinterpret_cast<SearchState*>(param);
    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);
    if (window_pid == state->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd)) {
      state->hwnd = hwnd;
      return FALSE;
    }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&state));
  return state.hwnd;
}

inline void release_render_target() {
  if (g_render_target) {
    g_render_target->Release();
    g_render_target = nullptr;
  }
}

inline void release_device_objects() {
  release_render_target();
  if (g_context) {
    g_context->Release();
    g_context = nullptr;
  }
  if (g_device) {
    g_device->Release();
    g_device = nullptr;
  }
}

inline void release_dx12_objects() {
  for (Dx12FrameContext& frame : g_dx12_frames) {
    if (frame.resource) frame.resource->Release();
    if (frame.allocator) frame.allocator->Release();
  }
  g_dx12_frames.clear();
  if (g_dx12_fence_event) {
    CloseHandle(g_dx12_fence_event);
    g_dx12_fence_event = nullptr;
  }
  if (g_dx12_fence) { g_dx12_fence->Release(); g_dx12_fence = nullptr; }
  if (g_dx12_command_list) { g_dx12_command_list->Release(); g_dx12_command_list = nullptr; }
  if (g_dx12_srv_heap) { g_dx12_srv_heap->Release(); g_dx12_srv_heap = nullptr; }
  if (g_dx12_rtv_heap) { g_dx12_rtv_heap->Release(); g_dx12_rtv_heap = nullptr; }
  if (g_dx12_device) { g_dx12_device->Release(); g_dx12_device = nullptr; }
  g_dx12_rtv_descriptor_size = 0;
  g_dx12_srv_descriptor_size = 0;
  g_dx12_srv_descriptors.fill(false);
  g_dx12_next_fence_value = 1;
  g_dx12_format = DXGI_FORMAT_UNKNOWN;
}

inline void restore_wndproc() {
  if (!g_hwnd || !g_old_wndproc) return;
  const LONG_PTR current = GetWindowLongPtr(g_hwnd, GWLP_WNDPROC);
  if (current == reinterpret_cast<LONG_PTR>(&wndproc)) {
    SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_old_wndproc));
  }
  g_old_wndproc = nullptr;
}

inline bool create_render_target(IDXGISwapChain* swap_chain) {
  release_render_target();
  if (!swap_chain || !g_device) return false;

  ID3D11Texture2D* back_buffer = nullptr;
  const HRESULT get_buffer = swap_chain->GetBuffer(
      0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
  if (FAILED(get_buffer) || !back_buffer) {
    log("DX11 swap chain back buffer unavailable; UI frame skipped.");
    return false;
  }

  D3D11_TEXTURE2D_DESC back_buffer_desc{};
  back_buffer->GetDesc(&back_buffer_desc);
  g_back_buffer_format = back_buffer_desc.Format;

  const HRESULT create_view = g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
  back_buffer->Release();
  if (FAILED(create_view) || !g_render_target) {
    log("DX11 render-target-view creation failed; UI frame skipped.");
    return false;
  }
  return true;
}

inline bool is_keyboard_message(UINT message) {
  return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
         message == WM_SYSKEYUP || message == WM_CHAR || message == WM_SYSCHAR;
}

inline bool is_keyboard_button_message(UINT message) {
  return message == WM_KEYDOWN || message == WM_KEYUP ||
         message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
}

inline bool is_keyboard_button_down(UINT message) {
  return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

inline bool is_client_mouse_message(UINT message) {
  return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

inline bool is_mouse_button_message(UINT message) {
  return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK ||
         message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
         message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MBUTTONDBLCLK ||
         message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK;
}

inline bool is_raw_input_message(UINT message) {
  return message == WM_INPUT;
}

inline int mouse_button_for_message(UINT message, WPARAM wparam) {
  switch (message) {
  case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return 0;
  case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return 1;
  case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return 2;
  case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
  default: return -1;
  }
}

inline bool is_mouse_button_down(UINT message) {
  return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
         message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
         message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
         message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
}

inline UINT mouse_button_message(int button, bool down) {
  static constexpr UINT down_messages[] = {
      WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN,
      WM_XBUTTONDOWN, WM_XBUTTONDOWN};
  static constexpr UINT up_messages[] = {
      WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP,
      WM_XBUTTONUP, WM_XBUTTONUP};
  if (button < 0 || button >= 5) return WM_NULL;
  return down ? down_messages[button] : up_messages[button];
}

inline LPARAM keyboard_transition_lparam(LPARAM source, bool down) {
  constexpr std::uintptr_t metadata_mask = 0x21ff0000u;
  constexpr std::uintptr_t released_mask = 0xc0000000u;
  const std::uintptr_t source_bits = static_cast<std::uintptr_t>(source);
  return static_cast<LPARAM>((source_bits & metadata_mask) | 1u |
                             (down ? 0u : released_mask));
}

inline WPARAM mouse_transition_wparam(int button, const std::array<MouseButtonState, 5>& states) {
  WORD flags = 0;
  if (states[0].down) flags |= MK_LBUTTON;
  if (states[1].down) flags |= MK_RBUTTON;
  if (states[2].down) flags |= MK_MBUTTON;
  if (states[3].down) flags |= MK_XBUTTON1;
  if (states[4].down) flags |= MK_XBUTTON2;
  if (g_physical_keyboard[VK_SHIFT].down) flags |= MK_SHIFT;
  if (g_physical_keyboard[VK_CONTROL].down) flags |= MK_CONTROL;
  const WORD xbutton = button == 3 ? XBUTTON1 : button == 4 ? XBUTTON2 : 0;
  return MAKEWPARAM(flags, xbutton);
}

inline void track_physical_input(UINT message, WPARAM wparam, LPARAM lparam) {
  if (is_keyboard_button_message(message) && wparam < g_physical_keyboard.size()) {
    KeyboardButtonState& state = g_physical_keyboard[static_cast<std::size_t>(wparam)];
    state.down = is_keyboard_button_down(message);
    state.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    if (state.down) state.lparam = lparam;
  }

  if (is_mouse_button_message(message)) {
    const int button = mouse_button_for_message(message, wparam);
    if (button >= 0) {
      MouseButtonState& state = g_physical_mouse[button];
      state.down = is_mouse_button_down(message);
      if (state.down) state.lparam = lparam;
    }
  } else if (message == WM_MOUSEMOVE) {
    for (MouseButtonState& state : g_physical_mouse)
      if (state.down) state.lparam = lparam;
  }
}

inline void track_game_input(UINT message, WPARAM wparam, LPARAM lparam) {
  if (is_keyboard_button_message(message) && wparam < g_game_keyboard.size()) {
    KeyboardButtonState& state = g_game_keyboard[static_cast<std::size_t>(wparam)];
    state.down = is_keyboard_button_down(message);
    state.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
    if (state.down) state.lparam = lparam;
  }

  if (!is_mouse_button_message(message)) return;
  const int button = mouse_button_for_message(message, wparam);
  if (button < 0) return;
  MouseButtonState& state = g_game_mouse[button];
  state.down = is_mouse_button_down(message);
  if (state.down) state.lparam = lparam;
}

inline void clear_tracked_input() {
  g_physical_keyboard.fill(KeyboardButtonState{});
  g_game_keyboard.fill(KeyboardButtonState{});
  g_physical_mouse.fill(MouseButtonState{});
  g_game_mouse.fill(MouseButtonState{});
}

inline void clear_game_input() {
  g_game_keyboard.fill(KeyboardButtonState{});
  g_game_mouse.fill(MouseButtonState{});
}

inline bool any_window_mouse_button_down() {
  for (bool down : g_wndproc_mouse_down)
    if (down) return true;
  return false;
}

inline void queue_input_event(const InputEvent& event) {
  std::lock_guard lock(g_input_mutex);
  if (event.kind == InputEventKind::mouse_position && !g_input_events.empty() &&
      g_input_events.back().kind == InputEventKind::mouse_position) {
    g_input_events.back() = event;
    return;
  }
  g_input_events.push_back(event);
}

inline void queue_release_all_mouse(HWND hwnd) {
  InputEvent event{};
  event.kind = InputEventKind::release_all_mouse;
  event.hwnd = hwnd;
  queue_input_event(event);
}

inline void queue_release_all_input(HWND hwnd) {
  InputEvent event{};
  event.kind = InputEventKind::release_all_input;
  event.hwnd = hwnd;
  queue_input_event(event);
}

inline void queue_mouse_position(HWND hwnd, LONG x, LONG y) {
  InputEvent event{};
  event.kind = InputEventKind::mouse_position;
  event.hwnd = hwnd;
  event.x = static_cast<float>(x);
  event.y = static_cast<float>(y);
  queue_input_event(event);
}

inline void queue_mouse_button(HWND hwnd, int button, bool down) {
  InputEvent event{};
  event.kind = InputEventKind::mouse_button;
  event.hwnd = hwnd;
  event.button = button;
  event.state = down;
  queue_input_event(event);
}

inline void clear_window_mouse_buttons(HWND hwnd, bool release_capture) {
  g_wndproc_mouse_down.fill(false);
  if (release_capture && GetCapture() == hwnd)
    ReleaseCapture();
  queue_release_all_mouse(hwnd);
}

inline void apply_window_input_transition(HWND hwnd, WNDPROC old_wndproc, bool open) {
  if (g_menu_input_open == open) {
    g_menu_visible.store(open, std::memory_order_release);
    return;
  }

  if (open) {
    if (ModConfig::is_il2cpp_backend) {
      // Unity clears its input-device state on focus loss. Reuse
      // that one-shot path instead of fabricating per-key transitions, which
      // does not reset the IL2CPP Input System's raw keyboard state.
      clear_game_input();
      if (old_wndproc && GetFocus() == hwnd) {
        CallWindowProc(old_wndproc, hwnd, WM_CANCELMODE, 0, 0);
        CallWindowProc(old_wndproc, hwnd, WM_KILLFOCUS, 0, 0);
        g_game_focus_suspended = true;
      }
    } else {
      g_menu_released_keyboard.fill(false);
      for (std::size_t key = 0; key < g_game_keyboard.size(); ++key) {
        KeyboardButtonState& game = g_game_keyboard[key];
        if (!game.down) continue;
        g_menu_released_keyboard[key] = true;
        game.down = false;
        const UINT message = game.system ? WM_SYSKEYUP : WM_KEYUP;
        CallWindowProc(old_wndproc, hwnd, message, static_cast<WPARAM>(key),
                       keyboard_transition_lparam(game.lparam, false));
      }

      g_menu_released_mouse.fill(false);
      for (int button = 0; button < static_cast<int>(g_game_mouse.size()); ++button) {
        MouseButtonState& game = g_game_mouse[button];
        if (!game.down) continue;
        g_menu_released_mouse[button] = true;
        const LPARAM position = game.lparam;
        game.down = false;
        CallWindowProc(old_wndproc, hwnd, mouse_button_message(button, false),
                       mouse_transition_wparam(button, g_game_mouse), position);
      }
    }
  } else {
    if (ModConfig::is_il2cpp_backend) {
      // Do not synthesize key or mouse presses while restoring game focus.
      // Unity resumes from its cleared state and accepts only real input.
      clear_game_input();
      if (g_game_focus_suspended && old_wndproc && GetFocus() == hwnd)
        CallWindowProc(old_wndproc, hwnd, WM_SETFOCUS, 0, 0);
      g_game_focus_suspended = false;
    } else {
      for (std::size_t key = 0; key < g_menu_released_keyboard.size(); ++key) {
        if (!g_menu_released_keyboard[key]) continue;
        const KeyboardButtonState& physical = g_physical_keyboard[key];
        if (physical.down) {
          g_game_keyboard[key] = physical;
          const UINT message = physical.system ? WM_SYSKEYDOWN : WM_KEYDOWN;
          CallWindowProc(old_wndproc, hwnd, message, static_cast<WPARAM>(key),
                         keyboard_transition_lparam(physical.lparam, true));
        }
        g_menu_released_keyboard[key] = false;
      }

      for (int button = 0; button < static_cast<int>(g_menu_released_mouse.size()); ++button) {
        if (!g_menu_released_mouse[button]) continue;
        const MouseButtonState& physical = g_physical_mouse[button];
        if (physical.down) {
          g_game_mouse[button] = physical;
          CallWindowProc(old_wndproc, hwnd, mouse_button_message(button, true),
                         mouse_transition_wparam(button, g_game_mouse), physical.lparam);
        }
        g_menu_released_mouse[button] = false;
      }
    }
  }

  g_menu_input_open = open;
  g_menu_visible.store(open, std::memory_order_release);
  queue_release_all_input(hwnd);
}

inline void drain_input_events() {
  std::deque<InputEvent> events;
  {
    std::lock_guard lock(g_input_mutex);
    events.swap(g_input_events);
  }

  ImGuiIO& io = ImGui::GetIO();
  for (const InputEvent& event : events) {
    switch (event.kind) {
    case InputEventKind::mouse_position:
      io.AddMousePosEvent(event.x, event.y);
      break;
    case InputEventKind::mouse_button:
      if (event.button >= 0 && event.button < 5)
        io.AddMouseButtonEvent(event.button, event.state);
      break;
    case InputEventKind::mouse_wheel:
      io.AddMouseWheelEvent(event.x, event.y);
      break;
    case InputEventKind::focus:
      io.AddFocusEvent(event.state);
      break;
    case InputEventKind::raw_message:
      ImGui_ImplWin32_WndProcHandler(event.hwnd, event.message,
                                     event.wparam, event.lparam);
      break;
    case InputEventKind::release_all_mouse:
      for (int button = 0; button < 5; ++button)
        io.AddMouseButtonEvent(button, false);
      break;
    case InputEventKind::release_all_input:
      io.ClearEventsQueue();
      io.ClearInputKeys();
      io.ClearInputMouse();
      break;
    }
  }
}

inline void log_cursor_error(const char* operation) {
  char text[192]{};
  std::snprintf(text, sizeof(text), "%s failed for the ImGui menu: Win32 error=%lu.",
                operation, static_cast<unsigned long>(GetLastError()));
  log(text);
}

inline bool set_native_cursor_open(HWND hwnd, bool open) {
  if (open) {
    SetLastError(ERROR_SUCCESS);
    HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    if (!arrow) {
      log_cursor_error("LoadCursor(IDC_ARROW)");
      return false;
    }
    if (!g_native_cursor_owned.load(std::memory_order_acquire)) {
      RECT clip{};
      SetLastError(ERROR_SUCCESS);
      if (!GetClipCursor(&clip)) {
        log_cursor_error("GetClipCursor");
        return false;
      }
      g_saved_cursor_clip = clip;
    }
    SetLastError(ERROR_SUCCESS);
    if (!ClipCursor(nullptr)) {
      log_cursor_error("ClipCursor(unlock)");
      return false;
    }
    if (!g_native_cursor_owned.load(std::memory_order_acquire)) {
      int display_count = -1;
      do {
        display_count = ShowCursor(TRUE);
        ++g_native_cursor_show_balance;
      } while (display_count < 0);
      g_saved_cursor = SetCursor(arrow);
      g_native_cursor_owned.store(true, std::memory_order_release);
    }
    POINT position{};
    if (GetCursorPos(&position) && ScreenToClient(hwnd, &position)) {
      g_virtual_mouse_position = position;
      g_virtual_mouse_position_valid = true;
      queue_mouse_position(hwnd, position.x, position.y);
    } else {
      log_cursor_error("GetCursorPos/ScreenToClient(menu open)");
      g_virtual_mouse_position_valid = false;
    }
    g_raw_mouse_seen = false;
    clear_window_mouse_buttons(hwnd, true);
    return true;
  }

  clear_window_mouse_buttons(hwnd, true);
  g_virtual_mouse_position_valid = false;
  g_raw_mouse_seen = false;
  if (!g_native_cursor_owned.load(std::memory_order_acquire)) return true;
  SetLastError(ERROR_SUCCESS);
  if (!ClipCursor(&g_saved_cursor_clip)) {
    log_cursor_error("ClipCursor(restore)");
    return false;
  }
  SetCursor(g_saved_cursor);
  g_saved_cursor = nullptr;
  while (g_native_cursor_show_balance > 0) {
    ShowCursor(FALSE);
    --g_native_cursor_show_balance;
  }
  g_native_cursor_owned.store(false, std::memory_order_release);
  return true;
}

inline bool request_window_cursor_state(bool open) {
  if (!g_hwnd || !IsWindow(g_hwnd)) return false;
  const UINT message = menu_cursor_message();
  if (!message) return false;
  const int requested = open ? 1 : 0;
  g_native_cursor_requested.store(requested, std::memory_order_release);
  const DWORD window_thread = GetWindowThreadProcessId(g_hwnd, nullptr);
  if (window_thread == GetCurrentThreadId()) {
    if (SendMessageW(g_hwnd, message, open ? 1 : 0, 0) != 0) return true;
    g_native_cursor_requested.store(-1, std::memory_order_release);
    return false;
  }

  SetLastError(ERROR_SUCCESS);
  if (!PostMessageW(g_hwnd, message, open ? 1 : 0, 0)) {
    g_native_cursor_requested.store(-1, std::memory_order_release);
    log_cursor_error("PostMessage(menu cursor state)");
    return false;
  }
  return true;
}

inline bool request_window_input_state(bool open) {
  if (!g_hwnd || !IsWindow(g_hwnd)) return false;
  const UINT message = menu_input_message();
  if (!message) return false;
  const int requested = open ? 1 : 0;
  g_menu_input_requested.store(requested, std::memory_order_release);
  const DWORD window_thread = GetWindowThreadProcessId(g_hwnd, nullptr);
  if (window_thread == GetCurrentThreadId()) {
    if (SendMessageW(g_hwnd, message, open ? 1 : 0, 0) != 0) return true;
    g_menu_input_requested.store(-1, std::memory_order_release);
    return false;
  }

  SetLastError(ERROR_SUCCESS);
  if (!PostMessageW(g_hwnd, message, open ? 1 : 0, 0)) {
    g_menu_input_requested.store(-1, std::memory_order_release);
    log_cursor_error("PostMessage(menu input state)");
    return false;
  }
  return true;
}

inline bool acquire_cursor_lease() {
  if (!URK::has_cursor_control()) return false;
  bool expected = false;
  if (!g_cursor_lease.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
    return true;
  }
  if (URK::set_menu_cursor_open(true)) return true;
  g_cursor_lease.store(false, std::memory_order_release);
  log("Failed to acquire the loader menu-cursor lease.");
  return false;
}

inline bool release_cursor_lease() {
  bool expected = true;
  if (!g_cursor_lease.compare_exchange_strong(expected, false,
                                               std::memory_order_acq_rel)) {
    return true;
  }
  if (URK::set_menu_cursor_open(false)) return true;
  g_cursor_lease.store(true, std::memory_order_release);
  log("Failed to release the loader menu-cursor lease.");
  return false;
}

inline void sync_menu_state() {
  const bool open = ModConfig::show_menu;
  if (g_menu_input_requested.load(std::memory_order_acquire) != (open ? 1 : 0) &&
      !request_window_input_state(open)) {
    log(open ? "Failed to suppress game input for the ImGui menu."
             : "Failed to restore game input after closing the ImGui menu.");
  }
  if (open)
    acquire_cursor_lease();
  else
    release_cursor_lease();

  const bool native_open = open && !g_cursor_lease.load(std::memory_order_acquire);
  if (g_native_cursor_requested.load(std::memory_order_acquire) != (native_open ? 1 : 0) &&
      !request_window_cursor_state(native_open)) {
    log(native_open ? "Failed to acquire native cursor ownership for the ImGui menu."
                    : "Failed to restore native cursor ownership after closing the ImGui menu.");
  }
  if (native_open && g_native_cursor_owned.load(std::memory_order_acquire)) {
    SetLastError(ERROR_SUCCESS);
    if (!ClipCursor(nullptr))
      log_cursor_error("ClipCursor(menu reapply)");
  }
}

inline void shutdown_imgui() {
  if (!request_window_input_state(false))
    log("Failed to restore game input during ImGui shutdown.");
  release_cursor_lease();
  if (!request_window_cursor_state(false))
    log("Failed to restore native cursor ownership during ImGui shutdown.");
  if (g_imgui_ready) {
    if (g_backend == GraphicsBackend::dx11) ImGui_ImplDX11_Shutdown();
    if (g_backend == GraphicsBackend::dx12) ImGui_ImplDX12_Shutdown();
    if (g_backend == GraphicsBackend::opengl) ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_ready = false;
  }
  {
    std::lock_guard lock(g_input_mutex);
    g_input_events.clear();
  }
  restore_wndproc();
  g_hwnd = nullptr;
  if (g_active_swap_chain) { g_active_swap_chain->Release(); g_active_swap_chain = nullptr; }
  release_device_objects();
  release_dx12_objects();
  if (g_dx12_command_queue) { g_dx12_command_queue->Release(); g_dx12_command_queue = nullptr; }
  g_gl_context = nullptr;
}

inline bool is_toggle_key_down(UINT message, WPARAM wparam, LPARAM lparam) {
  if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
  if (static_cast<int>(wparam) != ModConfig::menu_toggle_key) return false;
  constexpr LPARAM was_down_mask = 1LL << 30;
  return (lparam & was_down_mask) == 0;
}

inline void queue_menu_toggle() {
  g_menu_toggle_count.fetch_add(1, std::memory_order_acq_rel);
}

inline void relay_input_to_previous(HWND hwnd, WNDPROC old_wndproc,
                                    const InputRelay& input) {
  if (!old_wndproc) return;
  const UINT relay_message = input_relay_message();
  if (!relay_message) return;
  CallWindowProc(old_wndproc, hwnd, relay_message, 0,
                 reinterpret_cast<LPARAM>(&input));
}

inline bool queue_raw_mouse_input(HWND hwnd, LPARAM lparam) {
  RAWINPUTHEADER header{};
  UINT header_size = sizeof(header);
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_HEADER, &header,
                      &header_size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
    log_cursor_error("GetRawInputData(header)");
    return false;
  }
  if (header.dwType != RIM_TYPEMOUSE) return false;

  RAWINPUT input{};
  UINT input_size = sizeof(input);
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input,
                      &input_size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
    log_cursor_error("GetRawInputData(mouse)");
    return false;
  }

  RECT client{};
  if (!GetClientRect(hwnd, &client)) {
    log_cursor_error("GetClientRect(raw mouse)");
    return false;
  }
  if (client.right <= client.left || client.bottom <= client.top)
    return false;

  const RAWMOUSE& mouse = input.data.mouse;
  g_raw_mouse_seen = true;
  if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
    const bool virtual_desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
    const int origin_x = virtual_desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
    const int origin_y = virtual_desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
    const int width = GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
    const int height = GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
    POINT screen{origin_x + MulDiv(mouse.lLastX, width, 65535),
                 origin_y + MulDiv(mouse.lLastY, height, 65535)};
    if (!ScreenToClient(hwnd, &screen)) {
      log_cursor_error("ScreenToClient(raw mouse)");
      return false;
    }
    g_virtual_mouse_position = screen;
  } else {
    if (!g_virtual_mouse_position_valid) {
      g_virtual_mouse_position.x = (client.right - client.left) / 2;
      g_virtual_mouse_position.y = (client.bottom - client.top) / 2;
    }
    g_virtual_mouse_position.x += mouse.lLastX;
    g_virtual_mouse_position.y += mouse.lLastY;
  }

  g_virtual_mouse_position.x = (std::clamp)(g_virtual_mouse_position.x, client.left, client.right - 1);
  g_virtual_mouse_position.y = (std::clamp)(g_virtual_mouse_position.y, client.top, client.bottom - 1);
  g_virtual_mouse_position_valid = true;
  queue_mouse_position(hwnd, g_virtual_mouse_position.x, g_virtual_mouse_position.y);

  const USHORT buttons = mouse.usButtonFlags;
  if ((buttons & RI_MOUSE_LEFT_BUTTON_DOWN) != 0) queue_mouse_button(hwnd, 0, true);
  if ((buttons & RI_MOUSE_LEFT_BUTTON_UP) != 0) queue_mouse_button(hwnd, 0, false);
  if ((buttons & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0) queue_mouse_button(hwnd, 1, true);
  if ((buttons & RI_MOUSE_RIGHT_BUTTON_UP) != 0) queue_mouse_button(hwnd, 1, false);
  if ((buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0) queue_mouse_button(hwnd, 2, true);
  if ((buttons & RI_MOUSE_MIDDLE_BUTTON_UP) != 0) queue_mouse_button(hwnd, 2, false);
  if ((buttons & RI_MOUSE_BUTTON_4_DOWN) != 0) queue_mouse_button(hwnd, 3, true);
  if ((buttons & RI_MOUSE_BUTTON_4_UP) != 0) queue_mouse_button(hwnd, 3, false);
  if ((buttons & RI_MOUSE_BUTTON_5_DOWN) != 0) queue_mouse_button(hwnd, 4, true);
  if ((buttons & RI_MOUSE_BUTTON_5_UP) != 0) queue_mouse_button(hwnd, 4, false);
  return true;
}

inline void apply_pending_menu_toggle() {
  const unsigned toggles = g_menu_toggle_count.exchange(0, std::memory_order_acq_rel);
  if ((toggles & 1u) == 0)
    return;

  ModConfig::show_menu = !ModConfig::show_menu;
  sync_menu_state();
}

inline LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  WNDPROC old_wndproc = g_old_wndproc;
  InputRelay relayed_input{};
  const UINT relay_message = input_relay_message();
  const bool relayed = relay_message && message == relay_message;
  if (relayed) {
    const auto* input = reinterpret_cast<const InputRelay*>(lparam);
    if (!readable_range(input, sizeof(*input))) {
      log("Received an invalid ImGui input relay payload.");
      return FALSE;
    }
    relayed_input = *input;
    message = relayed_input.message;
    wparam = relayed_input.wparam;
    lparam = relayed_input.lparam;
  }

  const bool visible = g_menu_visible.load(std::memory_order_acquire) &&
                       !g_shutting_down.load(std::memory_order_acquire);
  bool consumed = false;

  const UINT cursor_message = menu_cursor_message();
  if (cursor_message && message == cursor_message) {
    const bool open = wparam != 0;
    if (set_native_cursor_open(hwnd, open)) return TRUE;
    int requested = open ? 1 : 0;
    g_native_cursor_requested.compare_exchange_strong(
        requested, -1, std::memory_order_acq_rel, std::memory_order_acquire);
    return FALSE;
  }

  const UINT input_message = menu_input_message();
  if (input_message && message == input_message) {
    apply_window_input_transition(hwnd, old_wndproc, wparam != 0);
    return TRUE;
  }

  track_physical_input(message, wparam, lparam);

  if (is_toggle_key_down(message, wparam, lparam)) {
    queue_menu_toggle();
    consumed = true;
  }

  if (message == WM_KILLFOCUS || message == WM_CANCELMODE) {
    clear_window_mouse_buttons(hwnd, true);
    if (message == WM_KILLFOCUS)
      clear_tracked_input();
    InputEvent focus{};
    focus.kind = InputEventKind::focus;
    focus.hwnd = hwnd;
    focus.state = false;
    queue_input_event(focus);
  } else if (message == WM_CAPTURECHANGED) {
    if (any_window_mouse_button_down())
      clear_window_mouse_buttons(hwnd, false);
  } else if (message == WM_SETFOCUS) {
    InputEvent focus{};
    focus.kind = InputEventKind::focus;
    focus.hwnd = hwnd;
    focus.state = true;
    queue_input_event(focus);
  }

  if (is_mouse_button_message(message)) {
    const int button = mouse_button_for_message(message, wparam);
    const bool down = is_mouse_button_down(message);
    const bool was_down = button >= 0 && g_wndproc_mouse_down[button];
    if (button >= 0 && down && visible) {
      if (!any_window_mouse_button_down() && GetCapture() != hwnd)
        SetCapture(hwnd);
      g_wndproc_mouse_down[button] = true;
      SetFocus(hwnd);
    } else if (button >= 0 && !down) {
      g_wndproc_mouse_down[button] = false;
      if (!any_window_mouse_button_down() && GetCapture() == hwnd)
        ReleaseCapture();
    }

    if ((down && visible) || !down || was_down) {
      queue_mouse_button(hwnd, button, down);
    }
    if (visible) consumed = true;
  } else if (message == WM_MOUSEMOVE && visible) {
    if (g_cursor_lease.load(std::memory_order_acquire) || !g_raw_mouse_seen) {
      g_virtual_mouse_position = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      g_virtual_mouse_position_valid = true;
      queue_mouse_position(hwnd, g_virtual_mouse_position.x, g_virtual_mouse_position.y);
    }
    consumed = true;
  } else if (message == WM_INPUT && visible) {
    if (!g_cursor_lease.load(std::memory_order_acquire))
      queue_raw_mouse_input(hwnd, lparam);
    consumed = true;
  } else if ((message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) && visible) {
    InputEvent event{};
    event.kind = InputEventKind::mouse_wheel;
    event.hwnd = hwnd;
    const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                        static_cast<float>(WHEEL_DELTA);
    event.x = message == WM_MOUSEHWHEEL ? -delta : 0.0f;
    event.y = message == WM_MOUSEWHEEL ? delta : 0.0f;
    queue_input_event(event);
    consumed = true;
  } else if (message == WM_MOUSELEAVE && visible) {
    InputEvent event{};
    event.kind = InputEventKind::mouse_position;
    event.hwnd = hwnd;
    event.x = -std::numeric_limits<float>::max();
    event.y = -std::numeric_limits<float>::max();
    queue_input_event(event);
    consumed = true;
  }

  if (visible && message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    consumed = true;
  }

  if (visible && is_keyboard_message(message)) {
    InputEvent event{};
    event.kind = InputEventKind::raw_message;
    event.hwnd = hwnd;
    event.message = message;
    event.wparam = wparam;
    event.lparam = lparam;
    queue_input_event(event);
    consumed = true;
  }

  if (visible && (is_client_mouse_message(message) || is_raw_input_message(message)))
    consumed = true;

  if (relayed || consumed) {
    const InputRelay input{message, wparam, lparam};
    relay_input_to_previous(hwnd, old_wndproc, input);
    return TRUE;
  }

  track_game_input(message, wparam, lparam);
  return old_wndproc ? CallWindowProc(old_wndproc, hwnd, message, wparam, lparam)
                     : DefWindowProc(hwnd, message, wparam, lparam);
}

inline bool init_dx11_imgui(IDXGISwapChain* swap_chain) {
  if (g_imgui_ready) return true;
  if (!swap_chain) {
    log("DX11 Present called without a swap chain; UI disabled for this frame.");
    return false;
  }

  if (FAILED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))) || !g_device) {
    log("DX11 device unavailable from swap chain; UI disabled.");
    return false;
  }

  g_device->GetImmediateContext(&g_context);
  if (!g_context) {
    log("DX11 immediate context unavailable; UI disabled.");
    release_device_objects();
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc{};
  if (!query_swap_chain_desc(swap_chain, &desc)) {
    log("DX11 swap chain description unavailable; UI disabled.");
    release_device_objects();
    return false;
  }
  query_swap_chain_color_space(swap_chain);

  g_hwnd = desc.OutputWindow ? desc.OutputWindow : find_main_window();
  if (!g_hwnd) {
    log("DX11 output window unavailable; UI disabled.");
    release_device_objects();
    return false;
  }

  if (!create_render_target(swap_chain)) {
    release_device_objects();
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  log_render_target_diagnostics(desc.BufferDesc.Format);
  apply_swap_chain_color_space(desc.BufferDesc.Format);
  ModUI::initialize_style();

  if (!ImGui_ImplWin32_Init(g_hwnd)) {
    log("ImGui Win32 backend initialization failed; UI disabled.");
    ImGui::DestroyContext();
    release_device_objects();
    return false;
  }

  if (!ImGui_ImplDX11_Init(g_device, g_context)) {
    log("ImGui DX11 backend initialization failed; UI disabled.");
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_device_objects();
    return false;
  }

  SetLastError(ERROR_SUCCESS);
  const LONG_PTR previous = SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc));
  if (!previous && GetLastError() != ERROR_SUCCESS) {
    log("WndProc replacement failed; UI disabled.");
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_device_objects();
    g_hwnd = nullptr;
    return false;
  }

  g_old_wndproc = reinterpret_cast<WNDPROC>(previous);
  swap_chain->AddRef();
  g_active_swap_chain = swap_chain;
  g_backend = GraphicsBackend::dx11;
  g_imgui_ready = true;
  sync_menu_state();
  log_font_diagnostics("DX11 ImGui", "linear");
  log("DX11 ImGui initialized with docking support.");
  return true;
}

inline bool create_dx12_device_objects(IDXGISwapChain* swap_chain) {
  IDXGISwapChain3* swap_chain3 = nullptr;
  if (!swap_chain || !g_dx12_device || !g_dx12_command_queue ||
      FAILED(swap_chain->QueryInterface(__uuidof(IDXGISwapChain3),
          reinterpret_cast<void**>(&swap_chain3))) || !swap_chain3) {
    log("DX12 swap chain or command queue unavailable; UI disabled.");
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc{};
  if (!query_swap_chain_desc(swap_chain, &desc) || desc.BufferCount == 0) {
    swap_chain3->Release();
    log("DX12 swap chain description unavailable; UI disabled.");
    return false;
  }
  g_dx12_format = desc.BufferDesc.Format;
  g_dx12_rtv_descriptor_size = g_dx12_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  g_dx12_srv_descriptor_size = g_dx12_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = desc.BufferCount;
  if (FAILED(g_dx12_device->CreateDescriptorHeap(&rtv_desc, __uuidof(ID3D12DescriptorHeap),
      reinterpret_cast<void**>(&g_dx12_rtv_heap))) || !g_dx12_rtv_heap) {
    swap_chain3->Release();
    log("DX12 RTV descriptor heap creation failed; UI disabled.");
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
  srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_desc.NumDescriptors = kDx12SrvDescriptorCapacity;
  srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(g_dx12_device->CreateDescriptorHeap(&srv_desc, __uuidof(ID3D12DescriptorHeap),
      reinterpret_cast<void**>(&g_dx12_srv_heap))) || !g_dx12_srv_heap) {
    swap_chain3->Release();
    log("DX12 shader-visible descriptor heap creation failed; UI disabled.");
    return false;
  }

  g_dx12_frames.resize(desc.BufferCount);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_dx12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  for (UINT index = 0; index < desc.BufferCount; ++index) {
    Dx12FrameContext& frame = g_dx12_frames[index];
    if (FAILED(g_dx12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&frame.allocator))) || !frame.allocator ||
        FAILED(swap_chain3->GetBuffer(index, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(&frame.resource))) || !frame.resource) {
      swap_chain3->Release();
      log("DX12 frame resource creation failed; UI disabled.");
      return false;
    }
    g_dx12_device->CreateRenderTargetView(frame.resource, nullptr, rtv);
    rtv.ptr += g_dx12_rtv_descriptor_size;
  }
  if (FAILED(g_dx12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      g_dx12_frames[0].allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void**>(&g_dx12_command_list))) || !g_dx12_command_list ||
      FAILED(g_dx12_command_list->Close()) ||
      FAILED(g_dx12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void**>(&g_dx12_fence))) || !g_dx12_fence) {
    swap_chain3->Release();
    log("DX12 command-list or fence creation failed; UI disabled.");
    return false;
  }
  g_dx12_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_dx12_fence_event) {
    swap_chain3->Release();
    log("DX12 fence event creation failed; UI disabled.");
    return false;
  }
  swap_chain3->Release();
  return true;
}

inline bool init_dx12_imgui(IDXGISwapChain* swap_chain) {
  if (g_imgui_ready) return g_backend == GraphicsBackend::dx12;
  if (!g_dx12_command_queue || FAILED(swap_chain->GetDevice(__uuidof(ID3D12Device),
      reinterpret_cast<void**>(&g_dx12_device))) || !g_dx12_device) {
    log("DX12 Present arrived before a graphics command queue was captured; UI will retry.");
    return false;
  }
  DXGI_SWAP_CHAIN_DESC desc{};
  if (!query_swap_chain_desc(swap_chain, &desc)) {
    log("DX12 output window unavailable; UI disabled.");
    release_dx12_objects();
    return false;
  }
  g_hwnd = desc.OutputWindow ? desc.OutputWindow : find_main_window();
  if (!g_hwnd || !create_dx12_device_objects(swap_chain)) {
    release_dx12_objects();
    return false;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_DockingEnable;
  ModUI::initialize_style();
  ImGui_ImplDX12_InitInfo dx12_init_info{};
  dx12_init_info.Device = g_dx12_device;
  dx12_init_info.CommandQueue = g_dx12_command_queue;
  dx12_init_info.NumFramesInFlight = static_cast<int>(g_dx12_frames.size());
  dx12_init_info.RTVFormat = g_dx12_format;
  dx12_init_info.SrvDescriptorHeap = g_dx12_srv_heap;
  dx12_init_info.SrvDescriptorAllocFn = &dx12_allocate_srv_descriptor;
  dx12_init_info.SrvDescriptorFreeFn = &dx12_free_srv_descriptor;
  if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX12_Init(&dx12_init_info)) {
    log("DX12 ImGui backend initialization failed; UI disabled.");
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_dx12_objects();
    return false;
  }
  SetLastError(ERROR_SUCCESS);
  const LONG_PTR previous = SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc));
  if (!previous && GetLastError() != ERROR_SUCCESS) {
    log("WndProc replacement failed; UI disabled.");
    ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    release_dx12_objects(); g_hwnd = nullptr;
    return false;
  }
  g_old_wndproc = reinterpret_cast<WNDPROC>(previous);
  swap_chain->AddRef();
  g_active_swap_chain = swap_chain;
  g_backend = GraphicsBackend::dx12;
  g_imgui_ready = true;
  sync_menu_state();
  log_font_diagnostics("DX12 ImGui", "linear");
  log("DX12 ImGui initialized with docking support.");
  return true;
}

inline bool init_imgui(IDXGISwapChain* swap_chain) {
  if (g_imgui_ready) return g_active_swap_chain == swap_chain;
  ID3D11Device* dx11_device = nullptr;
  const bool is_dx11 = SUCCEEDED(swap_chain->GetDevice(__uuidof(ID3D11Device),
      reinterpret_cast<void**>(&dx11_device))) && dx11_device;
  if (dx11_device) dx11_device->Release();
  return is_dx11 ? init_dx11_imgui(swap_chain) : init_dx12_imgui(swap_chain);
}

inline void render_dx12_frame(IDXGISwapChain* swap_chain) {
  IDXGISwapChain3* swap_chain3 = nullptr;
  if (!g_dx12_command_queue || !g_dx12_command_list || !g_dx12_fence ||
      FAILED(swap_chain->QueryInterface(__uuidof(IDXGISwapChain3),
          reinterpret_cast<void**>(&swap_chain3))) || !swap_chain3) return;
  const UINT frame_index = swap_chain3->GetCurrentBackBufferIndex();
  swap_chain3->Release();
  if (frame_index >= g_dx12_frames.size()) return;
  Dx12FrameContext& frame = g_dx12_frames[frame_index];
  if (frame.fence_value && g_dx12_fence->GetCompletedValue() < frame.fence_value) {
    if (FAILED(g_dx12_fence->SetEventOnCompletion(frame.fence_value, g_dx12_fence_event)) ||
        WaitForSingleObject(g_dx12_fence_event, INFINITE) != WAIT_OBJECT_0) {
      log("DX12 frame fence wait failed; UI frame skipped.");
      return;
    }
  }
  if (FAILED(frame.allocator->Reset()) || FAILED(g_dx12_command_list->Reset(frame.allocator, nullptr))) {
    log("DX12 command-list reset failed; UI frame skipped.");
    return;
  }
  ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); drain_input_events(); ImGui::NewFrame();
  ImGui::GetIO().MouseDrawCursor = false;
  ModUI::Highlight::manager().render(); ModUI::render_menu(); ImGui::Render();
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = frame.resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  g_dx12_command_list->ResourceBarrier(1, &barrier);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_dx12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  rtv.ptr += static_cast<SIZE_T>(frame_index) * g_dx12_rtv_descriptor_size;
  g_dx12_command_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  ID3D12DescriptorHeap* heaps[] = {g_dx12_srv_heap};
  g_dx12_command_list->SetDescriptorHeaps(1, heaps);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_dx12_command_list);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  g_dx12_command_list->ResourceBarrier(1, &barrier);
  if (FAILED(g_dx12_command_list->Close())) { log("DX12 command-list close failed; UI frame skipped."); return; }
  ID3D12CommandList* command_lists[] = {g_dx12_command_list};
  g_dx12_command_queue->ExecuteCommandLists(1, command_lists);
  frame.fence_value = g_dx12_next_fence_value++;
  if (FAILED(g_dx12_command_queue->Signal(g_dx12_fence, frame.fence_value)))
    log("DX12 fence signal failed; UI frame synchronization is unavailable.");
}

inline void render_frame(IDXGISwapChain* swap_chain) {
  RenderFrameGuard guard{};
  if (!guard.active) return;
  std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
  if (g_shutting_down.load(std::memory_order_acquire)) return;
  if (!init_imgui(swap_chain)) return;
  if (g_backend == GraphicsBackend::dx12) {
    apply_pending_menu_toggle();
    sync_menu_state();
    render_dx12_frame(swap_chain);
    return;
  }
  if (!g_context) {
    log("DX11 immediate context missing during Present; UI frame skipped.");
    return;
  }
  if (!g_render_target && !create_render_target(swap_chain)) return;
  apply_pending_menu_toggle();
  sync_menu_state();

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  drain_input_events();
  ImGui::NewFrame();
  ImGui::GetIO().MouseDrawCursor = false;
  ModUI::Highlight::manager().render();
  ModUI::render_menu();
  ImGui::Render();

  g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

inline HRESULT __stdcall detour_present(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
  struct ScopedPresentDepth {
    unsigned* depth = nullptr;
    explicit ScopedPresentDepth(unsigned* value) : depth(value) {
      if (depth) ++(*depth);
    }
    ~ScopedPresentDepth() {
      if (depth) --(*depth);
    }
    bool reentrant() const {
      return depth && *depth > 1;
    }
  };
  ScopedPresentDepth depth_guard(&g_present_depth);

  if (depth_guard.reentrant()) {
    return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
  }

  render_frame(swap_chain);
  return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
}

inline HRESULT __stdcall detour_present1(IDXGISwapChain1* swap_chain, UINT sync_interval, UINT flags,
                                        const DXGI_PRESENT_PARAMETERS* parameters) {
  struct ScopedPresentDepth {
    unsigned* depth = nullptr;
    explicit ScopedPresentDepth(unsigned* value) : depth(value) {
      if (depth) ++(*depth);
    }
    ~ScopedPresentDepth() {
      if (depth) --(*depth);
    }
    bool reentrant() const {
      return depth && *depth > 1;
    }
  };
  ScopedPresentDepth depth_guard(&g_present_depth);

  if (depth_guard.reentrant()) {
    return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
  }

  render_frame(swap_chain);
  return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
}

inline HRESULT __stdcall detour_resize_buffers(IDXGISwapChain* swap_chain, UINT buffer_count,
                                               UINT width, UINT height, DXGI_FORMAT format,
                                               UINT flags) {
  std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
  if (g_shutting_down.load(std::memory_order_acquire)) {
    return g_resize_buffers
        ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
        : DXGI_ERROR_INVALID_CALL;
  }
  if (g_imgui_ready && g_backend == GraphicsBackend::dx11) ImGui_ImplDX11_InvalidateDeviceObjects();
  if (g_imgui_ready && g_backend == GraphicsBackend::dx12) ImGui_ImplDX12_InvalidateDeviceObjects();
  if (g_backend == GraphicsBackend::dx11) release_render_target();
  if (g_backend == GraphicsBackend::dx12) release_dx12_objects();
  const HRESULT result = g_resize_buffers
      ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
      : DXGI_ERROR_INVALID_CALL;
  if (SUCCEEDED(result) && g_imgui_ready && g_backend == GraphicsBackend::dx12) {
    if (!g_dx12_device && FAILED(swap_chain->GetDevice(__uuidof(ID3D12Device),
        reinterpret_cast<void**>(&g_dx12_device)))) {
      log("DX12 device reacquisition failed after ResizeBuffers; UI resources unavailable.");
      return result;
    }
    if (create_dx12_device_objects(swap_chain)) {
      ImGui_ImplDX12_CreateDeviceObjects();
      log("DX12 swap chain resized; UI resources recreated.");
    } else {
      log("DX12 swap chain resized but UI resource recreation failed; retrying on next Present.");
    }
  } else if (SUCCEEDED(result) && g_imgui_ready) {
    if (create_render_target(swap_chain)) {
      ImGui_ImplDX11_CreateDeviceObjects();
      log("DX11 swap chain resized; UI render target recreated.");
    } else {
      log("DX11 swap chain resized but render target recreation failed; retrying on next Present.");
    }
  } else if (FAILED(result)) {
    log("DX11 ResizeBuffers failed; UI render target will be recreated on next Present.");
  }
  return result;
}

inline void __stdcall detour_execute_command_lists(ID3D12CommandQueue* command_queue, UINT count,
                                                    ID3D12CommandList* const* command_lists) {
  if (command_queue && command_queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT && !g_dx12_command_queue) {
    std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
    if (!g_dx12_command_queue) {
      command_queue->AddRef();
      g_dx12_command_queue = command_queue;
      log("DX12 graphics command queue captured.");
    }
  }
  if (g_execute_command_lists) g_execute_command_lists(command_queue, count, command_lists);
}

inline bool init_opengl_imgui(HDC device_context) {
  if (g_imgui_ready) return g_backend == GraphicsBackend::opengl && g_gl_context == wglGetCurrentContext();
  if (!device_context || !wglGetCurrentContext()) return false;
  g_hwnd = WindowFromDC(device_context);
  if (!g_hwnd) {
    log("OpenGL output window unavailable; UI disabled.");
    return false;
  }
  g_gl_context = wglGetCurrentContext();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_DockingEnable;
  ModUI::initialize_style();
  if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplOpenGL3_Init("#version 130")) {
    log("OpenGL ImGui backend initialization failed; UI disabled.");
    ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); g_hwnd = nullptr;
    return false;
  }
  SetLastError(ERROR_SUCCESS);
  const LONG_PTR previous = SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc));
  if (!previous && GetLastError() != ERROR_SUCCESS) {
    log("WndProc replacement failed; UI disabled.");
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); g_hwnd = nullptr;
    return false;
  }
  g_old_wndproc = reinterpret_cast<WNDPROC>(previous);
  g_backend = GraphicsBackend::opengl;
  g_imgui_ready = true;
  sync_menu_state();
  log_font_diagnostics("OpenGL ImGui", "linear");
  log("OpenGL ImGui initialized with docking support.");
  return true;
}

inline void render_opengl_frame(HDC device_context) {
  RenderFrameGuard guard{};
  if (!guard.active) return;
  std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
  if (g_shutting_down.load(std::memory_order_acquire) ||
      (g_backend != GraphicsBackend::none && g_backend != GraphicsBackend::opengl) ||
      !init_opengl_imgui(device_context)) return;
  apply_pending_menu_toggle();
  sync_menu_state();
  ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplWin32_NewFrame(); drain_input_events(); ImGui::NewFrame();
  ImGui::GetIO().MouseDrawCursor = false;
  ModUI::Highlight::manager().render(); ModUI::render_menu(); ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

inline BOOL WINAPI detour_wgl_swap_buffers(HDC device_context) {
  thread_local unsigned swap_depth = 0;
  ++swap_depth;
  if (swap_depth == 1) render_opengl_frame(device_context);
  --swap_depth;
  return g_wgl_swap_buffers ? g_wgl_swap_buffers(device_context) : FALSE;
}

inline bool capture_dxgi_targets(IDXGISwapChain* swap_chain, const char* probe_name,
                                 DxgiVTableTargets* targets) {
  if (!swap_chain || !targets || !readable_range(swap_chain, sizeof(void*))) {
    log("DXGI probe did not return a readable swap chain.");
    return false;
  }
  void** vtable = *reinterpret_cast<void***>(swap_chain);
  if (!readable_range(vtable, sizeof(void*) * 23)) {
    log("DXGI probe swap-chain vtable is unreadable; UI not installed.");
    return false;
  }
  targets->present = valid_hook_target(vtable[8]) ? vtable[8] : nullptr;
  targets->present1 = valid_hook_target(vtable[22]) ? vtable[22] : nullptr;
  targets->resize_buffers = valid_hook_target(vtable[13]) ? vtable[13] : nullptr;
  if ((targets->present || targets->present1) && targets->resize_buffers) return true;
  char text[160]{};
  std::snprintf(text, sizeof(text), "%s probe found non-executable swap-chain hook targets; UI not installed.",
                probe_name ? probe_name : "DXGI");
  log(text);
  return false;
}

inline DxgiVTableTargets discover_dxgi_targets(bool prefer_dx12) {
  DxgiVTableTargets targets{};
  char class_name[96]{};
  std::snprintf(class_name, sizeof(class_name), "URK_%s_Probe_%lu_%lu_%lu", prefer_dx12 ? "DX12" : "DX11",
                GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
  WNDCLASSEXA window_class{sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0,
                            GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr,
                            class_name, nullptr};
  if (!RegisterClassExA(&window_class)) {
    log("DX11 probe window class registration failed; UI not installed.");
    return targets;
  }

  HWND hwnd = CreateWindowA(window_class.lpszClassName, window_class.lpszClassName,
                            WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                            nullptr, nullptr, window_class.hInstance, nullptr);
  if (!hwnd) {
    log("DXGI probe window creation failed; UI not installed.");
    UnregisterClassA(window_class.lpszClassName, window_class.hInstance);
    return targets;
  }

  if (prefer_dx12) {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    IDXGISwapChain1* swap_chain1 = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                   reinterpret_cast<void**>(&device));
    if (SUCCEEDED(hr) && device) {
      D3D12_COMMAND_QUEUE_DESC queue_desc{};
      queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
      hr = device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
                                      reinterpret_cast<void**>(&queue));
    }
    if (SUCCEEDED(hr) && queue)
      hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr) && factory) {
      DXGI_SWAP_CHAIN_DESC1 swap_desc{};
      swap_desc.Width = 100;
      swap_desc.Height = 100;
      swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      swap_desc.SampleDesc.Count = 1;
      swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      swap_desc.BufferCount = 2;
      swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      hr = factory->CreateSwapChainForHwnd(queue, hwnd, &swap_desc, nullptr, nullptr, &swap_chain1);
    }
    if (SUCCEEDED(hr) && swap_chain1)
      hr = swap_chain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&swap_chain));
    if (SUCCEEDED(hr) && swap_chain)
      capture_dxgi_targets(swap_chain, "DX12", &targets);
    else {
      char text[160]{};
      std::snprintf(text, sizeof(text), "DX12 probe swap-chain creation failed (hr=0x%08X); UI not installed.",
                    static_cast<unsigned>(hr));
      log(text);
    }
    if (swap_chain) swap_chain->Release();
    if (swap_chain1) swap_chain1->Release();
    if (factory) factory->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(window_class.lpszClassName, window_class.hInstance);
    return targets;
  }

  DXGI_SWAP_CHAIN_DESC swap_desc{};
  swap_desc.BufferCount = 1;
  swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.OutputWindow = hwnd;
  swap_desc.SampleDesc.Count = 1;
  swap_desc.Windowed = TRUE;
  swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  constexpr D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };
  constexpr D3D_DRIVER_TYPE driver_types[] = {
      D3D_DRIVER_TYPE_HARDWARE,
      D3D_DRIVER_TYPE_WARP,
  };

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain* swap_chain = nullptr;
  D3D_FEATURE_LEVEL created_level{};
  HRESULT hr = E_FAIL;
  for (D3D_DRIVER_TYPE driver_type : driver_types) {
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, driver_type, nullptr, 0, feature_levels,
        static_cast<UINT>(sizeof(feature_levels) / sizeof(feature_levels[0])),
        D3D11_SDK_VERSION,
        &swap_desc, &swap_chain, &device, &created_level, &context);
    if (SUCCEEDED(hr) && swap_chain)
      break;
    if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
  }

  if (SUCCEEDED(hr) && swap_chain) {
    capture_dxgi_targets(swap_chain, "DX11", &targets);
  } else {
    char text[160]{};
    std::snprintf(text, sizeof(text),
                  "DXGI probe device creation failed (hr=0x%08X); DX11/DX12 overlay unavailable.",
                  static_cast<unsigned>(hr));
    log(text);
  }

  if (swap_chain) swap_chain->Release();
  if (context) context->Release();
  if (device) device->Release();
  DestroyWindow(hwnd);
  UnregisterClassA(window_class.lpszClassName, window_class.hInstance);
  return targets;
}

inline void* discover_dx12_execute_command_lists_target() {
  ID3D12Device* device = nullptr;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
      reinterpret_cast<void**>(&device))) || !device) {
    log("DX12 probe device creation failed; DX12 overlay will remain unavailable.");
    return nullptr;
  }
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ID3D12CommandQueue* queue = nullptr;
  const HRESULT result = device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void**>(&queue));
  void* target = nullptr;
  if (SUCCEEDED(result) && queue && readable_range(queue, sizeof(void*))) {
    void** vtable = *reinterpret_cast<void***>(queue);
    if (readable_range(vtable, sizeof(void*) * 11) && valid_hook_target(vtable[10])) {
      target = vtable[10];
    } else {
      log("DX12 ExecuteCommandLists target is unreadable or non-executable.");
    }
  } else {
    log("DX12 probe command queue creation failed; DX12 overlay will remain unavailable.");
  }
  if (queue) queue->Release();
  device->Release();
  return target;
}

inline bool install() {
  std::lock_guard<std::mutex> install_lock(g_install_mutex);
  g_shutting_down.store(false, std::memory_order_release);
  if (g_present_hooked || g_present1_hooked || g_wgl_swap_buffers_hooked) return true;
  if (!URK::hooks::available()) {
    log("hook API unavailable; UI not installed.");
    return false;
  }

  const std::int32_t graphics_device = URK::graphics_device_type();
  const bool probe_native_presentation = graphics_device == URK::graphics_device_unknown;
  const bool want_dx11 = probe_native_presentation ||
                        graphics_device == URK::graphics_device_direct3d11;
  const bool want_dx12 = probe_native_presentation ||
                        graphics_device == URK::graphics_device_direct3d12;
  const bool want_opengl = probe_native_presentation ||
                           graphics_device == URK::graphics_device_opengl2 ||
                           graphics_device == URK::graphics_device_openglcore;

  if (!want_dx11 && !want_dx12 && !want_opengl) {
    char text[160]{};
    std::snprintf(text, sizeof(text),
                  "Unsupported or unavailable Unity graphics device type (%d); UI render hook not installed.",
                  static_cast<int>(graphics_device));
    log(text);
    g_install_failed = true;
    return false;
  }

  if (probe_native_presentation)
    log("Unity graphics device type is unavailable; probing native DXGI and OpenGL presentation hooks.");

  if ((want_dx11 || want_dx12) && !g_dxgi_targets_discovered) {
    g_cached_dxgi_targets = discover_dxgi_targets(want_dx12);
    g_dxgi_targets_discovered = true;
  }
  const DxgiVTableTargets targets = g_cached_dxgi_targets;
  if ((want_dx11 || want_dx12) && (targets.present || targets.present1) && targets.resize_buffers) {
    if (targets.present) {
    g_present = reinterpret_cast<PresentFn>(targets.present);
    if (!URK::hooks::attach_ex(reinterpret_cast<void**>(&g_present), reinterpret_cast<void*>(&detour_present),
                               URK::hook_backend_auto)) {
      g_present = nullptr;
      log("DXGI Present hook attach failed; D3D overlay unavailable.");
    } else {
      g_present_hooked = true;
    }
    }
    if (targets.present1 && targets.present1 != targets.present) {
      g_present1 = reinterpret_cast<Present1Fn>(targets.present1);
      if (!URK::hooks::attach_ex(reinterpret_cast<void**>(&g_present1), reinterpret_cast<void*>(&detour_present1),
                                 URK::hook_backend_auto)) {
        g_present1 = nullptr;
        log("DXGI Present1 hook attach failed; D3D12 overlay may be unavailable.");
      } else {
        g_present1_hooked = true;
      }
    }
  }

  if (g_present_hooked || g_present1_hooked) {
    g_resize_buffers = reinterpret_cast<ResizeBuffersFn>(targets.resize_buffers);
    if (URK::hooks::attach_ex(reinterpret_cast<void**>(&g_resize_buffers),
                              reinterpret_cast<void*>(&detour_resize_buffers), URK::hook_backend_auto)) {
      g_resize_hooked = true;
    } else {
      log("DXGI ResizeBuffers hook attach failed; detaching presentation hooks for resize safety.");
      if (g_present1_hooked) {
        URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present1), reinterpret_cast<void*>(&detour_present1));
        g_present1 = nullptr;
        g_present1_hooked = false;
      }
      if (g_present_hooked) {
        URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present), reinterpret_cast<void*>(&detour_present));
        g_present = nullptr;
        g_present_hooked = false;
      }
    }
  } else if ((want_dx11 || want_dx12) && ((!targets.present && !targets.present1) || !targets.resize_buffers)) {
    log("DXGI Present, Present1, or ResizeBuffers unavailable; D3D overlay unavailable.");
  }

  if (want_dx12 && (g_present_hooked || g_present1_hooked)) {
    void* execute_target = discover_dx12_execute_command_lists_target();
    if (!execute_target) {
      log("DX12 ExecuteCommandLists unavailable; DX12 overlay unavailable.");
    } else {
    g_execute_command_lists = reinterpret_cast<ExecuteCommandListsFn>(execute_target);
    if (URK::hooks::attach_ex(reinterpret_cast<void**>(&g_execute_command_lists),
        reinterpret_cast<void*>(&detour_execute_command_lists), URK::hook_backend_auto)) {
      g_execute_command_lists_hooked = true;
    } else {
      g_execute_command_lists = nullptr;
      log("DX12 ExecuteCommandLists hook attach failed; DX12 overlay unavailable.");
    }
    }
    if (!g_execute_command_lists_hooked) {
      if (probe_native_presentation) {
        log("DX12 command queue hook unavailable during native probing; retaining DXGI Present for DX11.");
      } else {
        if (g_resize_hooked) {
          URK::hooks::detach_ex(reinterpret_cast<void**>(&g_resize_buffers),
                                reinterpret_cast<void*>(&detour_resize_buffers));
          g_resize_hooked = false;
        }
        if (g_present1_hooked) {
          URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present1),
                                reinterpret_cast<void*>(&detour_present1));
          g_present1_hooked = false;
        }
        if (g_present_hooked) {
          URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present),
                                reinterpret_cast<void*>(&detour_present));
          g_present_hooked = false;
        }
        g_present = nullptr;
        g_present1 = nullptr;
        g_resize_buffers = nullptr;
      }
    }
  }

  if (want_opengl) {
  HMODULE opengl32 = GetModuleHandleA("opengl32.dll");
  if (opengl32) {
    g_wgl_swap_buffers = reinterpret_cast<WglSwapBuffersFn>(GetProcAddress(opengl32, "wglSwapBuffers"));
    if (g_wgl_swap_buffers && URK::hooks::attach_ex(reinterpret_cast<void**>(&g_wgl_swap_buffers),
        reinterpret_cast<void*>(&detour_wgl_swap_buffers), URK::hook_backend_auto)) {
      g_wgl_swap_buffers_hooked = true;
    } else {
      g_wgl_swap_buffers = nullptr;
      log("OpenGL wglSwapBuffers hook attach failed; OpenGL overlay unavailable.");
    }
  } else {
    log("opengl32.dll is not loaded by the process; OpenGL overlay unavailable.");
  }
  }

  if (!g_present_hooked && !g_present1_hooked && !g_wgl_swap_buffers_hooked) {
    log("No supported render presentation hook could be installed.");
    return false;
  }
  if (probe_native_presentation)
    log("Native presentation hooks installed; UI will initialize on the first compatible render context.");
  else if (want_dx11)
    log("DX11 render hooks installed by generated project.");
  else if (want_dx12)
    log("DX12 render hooks installed by generated project.");
  else
    log("OpenGL render hooks installed by generated project.");
  return true;
}

inline void unregister_install_callback() {
  if (!g_install_callback_registered || !g_mod_context)
    return;
  if (g_mod_context->size >= offsetof(URK_ModContext, MainThreadUnregister) +
          sizeof(g_mod_context->MainThreadUnregister) &&
      g_mod_context->MainThreadUnregister) {
    g_mod_context->MainThreadUnregister(&install_on_main_thread);
  }
  g_install_callback_registered = false;
}

inline void install_on_main_thread() {
  if (g_shutting_down.load(std::memory_order_acquire)) {
    unregister_install_callback();
    return;
  }
  if (g_install_complete || g_install_failed) {
    unregister_install_callback();
    return;
  }

  if (install()) {
    g_install_complete = true;
    unregister_install_callback();
    return;
  }

  ++g_graphics_probe_attempts;
  if (g_install_failed || g_graphics_probe_attempts >= kMaxGraphicsProbeAttempts) {
    if (!g_install_failed)
      log("Unity graphics device type did not become available; UI render hook not installed.");
    g_install_failed = true;
    unregister_install_callback();
  }
}

inline bool install(const URK_ModContext* ctx) {
  g_mod_context = ctx;
  URK::set_context(ctx);
  if (g_install_complete)
    return true;
  if (g_install_failed)
    return false;
  if (!ctx) {
    log("mod context is unavailable; UI render hook not installed.");
    g_install_failed = true;
    return false;
  }
  if (ctx->size < offsetof(URK_ModContext, MainThreadRegister) +
          sizeof(ctx->MainThreadRegister) ||
      !ctx->MainThreadRegister || !URK::has_main_thread()) {
    log("main-thread dispatcher unavailable; installing native presentation hooks without Unity graphics-device query.");
    if (!install()) {
      log("native presentation hook installation failed; UI render hook not installed.");
      g_install_failed = true;
      return false;
    }
    g_install_complete = true;
    return true;
  }
  if (!g_install_callback_registered) {
    g_install_callback_registered = ctx->MainThreadRegister(&install_on_main_thread) != 0;
    if (!g_install_callback_registered) {
      log("main-thread render hook install callback registration failed.");
      g_install_failed = true;
      return false;
    }
    log("graphics-specific ImGui hook selection scheduled on the Unity main thread.");
  }
  return true;
}

inline bool uninstall() {
  g_shutting_down.store(true, std::memory_order_release);
  unregister_install_callback();
  g_menu_toggle_count.store(0, std::memory_order_release);
  bool all_hooks_detached = true;
  if (g_wgl_swap_buffers_hooked) {
    if (URK::hooks::detach_ex(reinterpret_cast<void**>(&g_wgl_swap_buffers), reinterpret_cast<void*>(&detour_wgl_swap_buffers))) {
      g_wgl_swap_buffers_hooked = false;
      log("OpenGL wglSwapBuffers hook detached.");
    } else all_hooks_detached = false;
  }
  if (g_execute_command_lists_hooked) {
    if (URK::hooks::detach_ex(reinterpret_cast<void**>(&g_execute_command_lists),
                              reinterpret_cast<void*>(&detour_execute_command_lists))) {
      g_execute_command_lists_hooked = false;
      log("DX12 ExecuteCommandLists hook detached.");
    } else all_hooks_detached = false;
  }
  if (g_resize_hooked) {
    if (URK::hooks::detach_ex(reinterpret_cast<void**>(&g_resize_buffers), reinterpret_cast<void*>(&detour_resize_buffers))) {
      g_resize_hooked = false;
      log("DXGI ResizeBuffers hook detached.");
    } else all_hooks_detached = false;
  }
  if (g_present_hooked) {
    if (URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present), reinterpret_cast<void*>(&detour_present))) {
      g_present_hooked = false;
      log("DXGI Present hook detached.");
    } else all_hooks_detached = false;
  }
  if (g_present1_hooked) {
    if (URK::hooks::detach_ex(reinterpret_cast<void**>(&g_present1), reinterpret_cast<void*>(&detour_present1))) {
      g_present1_hooked = false;
      log("DXGI Present1 hook detached.");
    } else all_hooks_detached = false;
  }
  if (!all_hooks_detached) {
    log("Render hook detach was incomplete; skipping ImGui teardown while the loader retains the module.");
    return false;
  }

  const DWORD current_thread = GetCurrentThreadId();
  const DWORD render_thread = g_render_thread_id.load(std::memory_order_acquire);
  if (current_thread != render_thread) {
    for (unsigned attempt = 0; attempt < 200 && g_active_frames.load(std::memory_order_acquire) != 0; ++attempt) {
      Sleep(1);
    }
    if (g_active_frames.load(std::memory_order_acquire) != 0) {
      log("Render UI shutdown timed out waiting for an active frame; skipping ImGui teardown to avoid deadlock.");
      g_present = nullptr;
      g_present1 = nullptr;
      g_resize_buffers = nullptr;
      return false;
    }
  }

  {
    std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
    shutdown_imgui();
  }
  g_present = nullptr;
  g_present1 = nullptr;
  g_resize_buffers = nullptr;
  g_execute_command_lists = nullptr;
  g_wgl_swap_buffers = nullptr;
  g_render_thread_id.store(0, std::memory_order_release);
  g_install_complete = false;
  g_install_failed = false;
  g_graphics_probe_attempts = 0;
  g_mod_context = nullptr;
  return true;
}
} // namespace ModRenderHook
