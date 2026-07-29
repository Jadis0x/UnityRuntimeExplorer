// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "method_tracer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Explorer::MethodTraceFormat {

struct ArgumentView {
    std::size_t index = 0;
    std::string name;
    std::string type;
    std::string value;
    std::string raw_value;
    std::string raw_abi;
    bool inspectable_reference = false;
    bool readable = false;
};

double elapsed_seconds(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record);
std::string elapsed_text(double seconds);
std::string address(std::uintptr_t value);
std::vector<ArgumentView> arguments(const MethodTracer::Snapshot& trace,
                                    const MethodTracer::Record& record);
std::string argument_summary(const MethodTracer::Snapshot& trace,
                             const MethodTracer::Record& record);
std::string raw_arguments(const MethodTracer::Snapshot& trace,
                          const MethodTracer::Record& record, bool include_abi_lanes);
std::string result(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record);
std::string raw_result(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record);
std::string csv(const MethodTracer::Snapshot& trace);
std::string json(const MethodTracer::Snapshot& trace);

} // namespace Explorer::MethodTraceFormat
