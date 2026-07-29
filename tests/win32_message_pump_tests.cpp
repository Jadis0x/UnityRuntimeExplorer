#include "mod/hooks/win32_message_pump.h"
#include "mod/hooks/win32_input_coordinates.h"

#include <array>
#include <atomic>
#include <cassert>

namespace {

	constexpr wchar_t k_test_window_class[] = L"URKMessagePumpContractWindow";
	constexpr UINT k_test_message = WM_APP + 41;
	std::atomic_uint g_received_messages{0};

	LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
		if (message == k_test_message) {
			g_received_messages.fetch_add(1, std::memory_order_relaxed);
			return 0;
		}
		return DefWindowProcW(window, message, wparam, lparam);
	}

}

int main() {
	WNDCLASSEXW window_class{};
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = &test_wndproc;
	window_class.hInstance = GetModuleHandleW(nullptr);
	window_class.lpszClassName = k_test_window_class;
	RegisterClassExW(&window_class);

	const HWND window = CreateWindowExW(
		0, k_test_window_class, L"", WS_POPUP,
		257, 193, 32, 32, nullptr, nullptr, window_class.hInstance, nullptr);
	assert(window);
	assert(!ModRenderHook::is_imgui_platform_window(window));
	assert(SetPropA(window, "IMGUI_CONTEXT", reinterpret_cast<HANDLE>(1)));
	assert(ModRenderHook::is_imgui_platform_window(window));
	assert(RemovePropA(window, "IMGUI_CONTEXT") == reinterpret_cast<HANDLE>(1));

	POINT client_position{7, 11};
	POINT desktop_position = client_position;
	assert(ModRenderHook::client_mouse_to_desktop(window, &desktop_position));
	assert(desktop_position.x != client_position.x ||
		desktop_position.y != client_position.y);
	POINT viewport_position = desktop_position;
	assert(ModRenderHook::desktop_mouse_to_imgui(window, true, &viewport_position));
	assert(viewport_position.x == desktop_position.x);
	assert(viewport_position.y == desktop_position.y);
	assert(ModRenderHook::desktop_mouse_to_imgui(window, false, &desktop_position));
	assert(desktop_position.x == client_position.x);
	assert(desktop_position.y == client_position.y);

	assert(PostMessageW(window, k_test_message, 0, 0));
	assert(PostMessageW(window, k_test_message, 0, 0));

	const std::array windows{window, window};
	const auto first = ModRenderHook::pump_owned_window_messages(windows, 1);
	assert(first.dispatched == 1);
	assert(first.foreign_thread_windows == 0);
	assert(first.backlog_remaining);
	assert(g_received_messages.load(std::memory_order_relaxed) == 1);

	const auto second = ModRenderHook::pump_owned_window_messages(windows);
	assert(second.dispatched == 1);
	assert(!second.backlog_remaining);
	assert(g_received_messages.load(std::memory_order_relaxed) == 2);

	DestroyWindow(window);
	UnregisterClassW(k_test_window_class, window_class.hInstance);
	return 0;
}
