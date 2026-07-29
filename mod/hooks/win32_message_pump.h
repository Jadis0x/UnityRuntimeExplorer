#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

	struct WindowMessagePumpResult {
		std::uint32_t dispatched = 0;
		std::uint32_t foreign_thread_windows = 0;
		bool backlog_remaining = false;
	};

	// Processes only messages addressed to the supplied HWNDs. Game-window and
	// thread-level messages remain in Unity's queue.
	[[nodiscard]] WindowMessagePumpResult pump_owned_window_messages(
		std::span<const HWND> windows,
		std::size_t message_budget = 512);

	// Dear ImGui's Win32 backend marks every owned secondary viewport HWND with
	// this process-local property. It can be queried from Unity's window thread
	// without touching the ImGui context.
	[[nodiscard]] bool is_imgui_platform_window(HWND window);

}
