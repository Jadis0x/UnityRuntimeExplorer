// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_stdio_server.h"

#include <Windows.h>

#include <charconv>
#include <iostream>
#include <optional>
#include <string_view>

int main(int argc, char** argv) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    std::optional<std::uint32_t> game_pid;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help") {
            std::cerr << "Usage: URK_UnityRuntimeExplorer_McpServer.exe [--game-pid <pid>]\n";
            return 0;
        }
        if (argument == "--game-pid" && index + 1 < argc) {
            std::uint32_t parsed = 0;
            const std::string_view value = argv[++index];
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) {
                std::cerr << "--game-pid requires a positive integer\n";
                return 2;
            }
            game_pid = parsed;
            continue;
        }
        std::cerr << "Unknown argument: " << argument << '\n';
        return 2;
    }
    Explorer::Mcp::StdioServer server(game_pid);
    return server.run();
}
