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

    std::cout << "method trace ABI contract passed\n";
    return 0;
}
