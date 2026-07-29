#pragma once

#include <optional>

#include <dxgi.h>

namespace ModRenderHook {

	struct Dx11ViewportSwapChainConfig {
		DXGI_SWAP_CHAIN_DESC descriptor{};
		bool flip_model = false;
	};

	// Builds a secondary-viewport descriptor from the game's real swap chain.
	// Matching the swap-effect model is required: mixing a flip-model game
	// chain with a legacy discard viewport chain can block indefinitely in
	// IDXGISwapChain::Present on some drivers.
	[[nodiscard]] std::optional<Dx11ViewportSwapChainConfig>
	make_dx11_viewport_swap_chain_config(const DXGI_SWAP_CHAIN_DESC& game_descriptor);

	[[nodiscard]] const char* dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect);

}
