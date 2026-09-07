// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

struct URK_ModContext;

namespace ModRenderHook {

bool install(const URK_ModContext* context);
bool uninstall();

// The hooked game's ID3D11Device, for building custom GPU resources (e.g.
// inspector texture previews). Returns null until a DX11 frame has rendered,
// and always null when the game renders through DX12 or OpenGL instead.
void* dx11_device();

} // namespace ModRenderHook
