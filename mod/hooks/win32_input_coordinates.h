#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

	// Input collected from Unity's main HWND is normalized to desktop
	// coordinates. Dear ImGui requires that coordinate space when platform
	// viewports are enabled.
	[[nodiscard]] bool client_mouse_to_desktop(HWND window, POINT* position) noexcept;

	// Converts the canonical desktop position to the coordinate space expected
	// by the active ImGui mode. Multi-viewport input remains desktop-relative;
	// single-viewport input becomes relative to the main client area.
	[[nodiscard]] bool desktop_mouse_to_imgui(
		HWND window,
		bool multi_viewport,
		POINT* position) noexcept;

}
