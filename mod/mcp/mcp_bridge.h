// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <string>

namespace Explorer { class RuntimeModel; }

namespace Explorer::Mcp::Bridge {

bool start(std::string& error);
void tick(RuntimeModel& model);
void stop();
bool running();

} // namespace Explorer::Mcp::Bridge
