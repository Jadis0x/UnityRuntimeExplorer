#include "dx11_viewport_swap_chain.h"

#include <algorithm>

namespace ModRenderHook {

	namespace {

		[[nodiscard]] bool is_known_swap_effect(DXGI_SWAP_EFFECT effect) {
			switch (effect) {
			case DXGI_SWAP_EFFECT_DISCARD:
			case DXGI_SWAP_EFFECT_SEQUENTIAL:
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
			case DXGI_SWAP_EFFECT_FLIP_DISCARD:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool is_flip_model(DXGI_SWAP_EFFECT effect) {
			return effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
				effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
		}

	}

	std::optional<Dx11ViewportSwapChainConfig>
	make_dx11_viewport_swap_chain_config(const DXGI_SWAP_CHAIN_DESC& game_descriptor) {
		if (!is_known_swap_effect(game_descriptor.SwapEffect) ||
			game_descriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN)
			return std::nullopt;

		Dx11ViewportSwapChainConfig config{};
		config.flip_model = is_flip_model(game_descriptor.SwapEffect);

		DXGI_SWAP_CHAIN_DESC& viewport = config.descriptor;
		viewport.BufferDesc.Width = 0;
		viewport.BufferDesc.Height = 0;
		viewport.BufferDesc.RefreshRate = {0, 1};
		viewport.BufferDesc.Format = game_descriptor.BufferDesc.Format;
		viewport.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		viewport.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		viewport.SampleDesc = {1, 0};
		viewport.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		viewport.BufferCount = config.flip_model
			? (std::clamp)(game_descriptor.BufferCount, 2u, 16u)
			: (std::clamp)(game_descriptor.BufferCount, 1u, 16u);
		viewport.OutputWindow = nullptr;
		viewport.Windowed = TRUE;
		viewport.SwapEffect = game_descriptor.SwapEffect;

		// Fullscreen/tearing/GDI flags describe the game's presentation policy,
		// not an ImGui tool window. Copying them can make CreateSwapChain reject
		// an otherwise compatible windowed descriptor.
		viewport.Flags = 0;
		return config;
	}

	const char* dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect) {
		switch (effect) {
		case DXGI_SWAP_EFFECT_DISCARD:
			return "DISCARD";
		case DXGI_SWAP_EFFECT_SEQUENTIAL:
			return "SEQUENTIAL";
		case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
			return "FLIP_SEQUENTIAL";
		case DXGI_SWAP_EFFECT_FLIP_DISCARD:
			return "FLIP_DISCARD";
		default:
			return "UNKNOWN";
		}
	}

}
