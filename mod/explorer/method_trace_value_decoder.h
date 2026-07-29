#pragma once

#include "method_tracer.h"

namespace Explorer::MethodTraceValueDecoder {

// Resolves raw ABI snapshots through the active Mono/IL2CPP metadata API.
// This runs on the Explorer thread, never in the detour/game-call path.
void resolve_displays(MethodTracer::Snapshot& trace);

}
