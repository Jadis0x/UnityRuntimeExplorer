#include "win32_message_pump.h"

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <vector>

namespace ModRenderHook {

	namespace {

		[[nodiscard]] bool contains_window(std::span<const HWND> windows, std::size_t end, HWND candidate) {
			return std::find(
				windows.begin(),
				windows.begin() + static_cast<std::ptrdiff_t>(end),
				candidate) != windows.begin() + static_cast<std::ptrdiff_t>(end);
		}

	}

	WindowMessagePumpResult pump_owned_window_messages(
		std::span<const HWND> windows,
		std::size_t message_budget) {
		WindowMessagePumpResult result{};
		if (windows.empty() || message_budget == 0)
			return result;

		const DWORD current_thread = GetCurrentThreadId();
		thread_local std::vector<HWND> owned_windows;
		owned_windows.clear();
		owned_windows.reserve(windows.size());

		for (std::size_t index = 0; index < windows.size(); ++index) {
			const HWND window = windows[index];
			if (!window || !IsWindow(window) || contains_window(windows, index, window))
				continue;
			if (GetWindowThreadProcessId(window, nullptr) != current_thread) {
				++result.foreign_thread_windows;
				continue;
			}
			owned_windows.push_back(window);
		}

		MSG message{};
		std::size_t remaining = message_budget;
		bool made_progress = true;
		while (remaining != 0 && made_progress) {
			made_progress = false;
			for (const HWND window : owned_windows) {
				if (remaining == 0)
					break;
				if (!PeekMessageW(&message, window, 0, 0, PM_REMOVE))
					continue;
				TranslateMessage(&message);
				DispatchMessageW(&message);
				++result.dispatched;
				--remaining;
				made_progress = true;
			}
		}

		if (remaining == 0) {
			for (const HWND window : owned_windows) {
				if (PeekMessageW(&message, window, 0, 0, PM_NOREMOVE)) {
					result.backlog_remaining = true;
					break;
				}
			}
		}
		return result;
	}

	bool is_imgui_platform_window(HWND window) {
		if (!window || !IsWindow(window))
			return false;
		DWORD process_id = 0;
		GetWindowThreadProcessId(window, &process_id);
		if (process_id != GetCurrentProcessId())
			return false;
		// The window class, not the IMGUI_CONTEXT property:
		// ImGui_ImplWin32_Init() stamps that property on the game's *main*
		// window too, so testing it would report the game window as an ImGui
		// viewport as soon as any mod in the process initializes its backend.
		wchar_t class_name[32]{};
		const int length = GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
		return length > 0 && std::wcscmp(class_name, L"ImGui Platform") == 0;
	}

}
