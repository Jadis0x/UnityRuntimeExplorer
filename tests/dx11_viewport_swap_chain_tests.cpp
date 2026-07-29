#include "mod/hooks/dx11_viewport_swap_chain.h"

#include <cassert>

int main() {
	DXGI_SWAP_CHAIN_DESC flip_game{};
	flip_game.BufferDesc.Width = 1920;
	flip_game.BufferDesc.Height = 1080;
	flip_game.BufferDesc.RefreshRate = {180, 1};
	flip_game.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	flip_game.SampleDesc = {1, 0};
	flip_game.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
	flip_game.BufferCount = 3;
	flip_game.OutputWindow = reinterpret_cast<HWND>(1);
	flip_game.Windowed = TRUE;
	flip_game.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	flip_game.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	const auto flip = ModRenderHook::make_dx11_viewport_swap_chain_config(flip_game);
	assert(flip);
	assert(flip->flip_model);
	assert(flip->descriptor.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD);
	assert(flip->descriptor.BufferCount == 3);
	assert(flip->descriptor.BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
	assert(flip->descriptor.BufferDesc.Width == 0);
	assert(flip->descriptor.BufferDesc.Height == 0);
	assert(flip->descriptor.BufferDesc.RefreshRate.Numerator == 0);
	assert(flip->descriptor.SampleDesc.Count == 1);
	assert(flip->descriptor.BufferUsage == DXGI_USAGE_RENDER_TARGET_OUTPUT);
	assert(flip->descriptor.OutputWindow == nullptr);
	assert(flip->descriptor.Windowed);
	assert(flip->descriptor.Flags == 0);

	flip_game.BufferCount = 1;
	const auto undersized_flip = ModRenderHook::make_dx11_viewport_swap_chain_config(flip_game);
	assert(undersized_flip);
	assert(undersized_flip->descriptor.BufferCount == 2);

	DXGI_SWAP_CHAIN_DESC legacy_game{};
	legacy_game.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	legacy_game.BufferCount = 1;
	legacy_game.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	const auto legacy = ModRenderHook::make_dx11_viewport_swap_chain_config(legacy_game);
	assert(legacy);
	assert(!legacy->flip_model);
	assert(legacy->descriptor.SwapEffect == DXGI_SWAP_EFFECT_DISCARD);
	assert(legacy->descriptor.BufferCount == 1);

	DXGI_SWAP_CHAIN_DESC invalid = legacy_game;
	invalid.BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	assert(!ModRenderHook::make_dx11_viewport_swap_chain_config(invalid));
	invalid = legacy_game;
	invalid.SwapEffect = static_cast<DXGI_SWAP_EFFECT>(99);
	assert(!ModRenderHook::make_dx11_viewport_swap_chain_config(invalid));
	return 0;
}
