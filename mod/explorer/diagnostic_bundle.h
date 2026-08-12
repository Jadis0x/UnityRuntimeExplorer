// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <string>

namespace Explorer {
struct Snapshot;

namespace DiagnosticBundle {
struct Result {
    bool succeeded = false;
    std::string path;
    std::string error;
};

Result write(const Snapshot& snapshot);
} // namespace DiagnosticBundle
} // namespace Explorer
