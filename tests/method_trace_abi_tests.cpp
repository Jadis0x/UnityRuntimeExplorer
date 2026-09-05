// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod/explorer/method_trace_abi.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    Explorer::MethodTraceAbi::RegisterFrame frame{};
    frame.rcx = 10;
    frame.rdx = 20;
    frame.r8 = 30;
    frame.r9 = 40;
    frame.shadow_space = {100, 200, 300, 400};
    frame.stack_arguments[0] = 50;
    frame.stack_arguments[1] = 60;
    frame.stack_arguments[2] = 70;

    require(Explorer::MethodTraceAbi::integer_argument(frame, 0) == 10,
            "first Win64 register argument");
    require(Explorer::MethodTraceAbi::integer_argument(frame, 3) == 40,
            "fourth Win64 register argument");
    require(Explorer::MethodTraceAbi::integer_argument(frame, 4) == 50,
            "first stack argument must skip the home area");
    require(Explorer::MethodTraceAbi::integer_argument(frame, 6) == 70,
            "subsequent stack arguments");

    frame.xmm[1][0] = 0x2a;
    require(Explorer::MethodTraceAbi::xmm_argument(frame, 1, 0) == 0x2a,
            "register floating arguments come from the xmm save area");
    require(Explorer::MethodTraceAbi::xmm_argument(frame, 4, 0) == 50,
            "stack floating arguments come from their stack slot");
    require(Explorer::MethodTraceAbi::xmm_argument(frame, 4, 8) == 0,
            "a stack slot has no high lane");

    constexpr std::size_t beyond = URK::Unity::Inspect::kMaxMethodParameters + 4;
    require(Explorer::MethodTraceAbi::integer_argument(frame, beyond) == 0,
            "slots past the captured stack window are not read");
    require(Explorer::MethodTraceAbi::xmm_argument(frame, beyond, 0) == 0,
            "floating slots past the captured stack window are not read");

    std::cout << "method trace ABI contract passed\n";
    return 0;
}
