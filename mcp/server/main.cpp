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
    bool allow_tracing = false;
    bool allow_invocation = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help") {
            std::cerr << "UnityRuntimeExplorer MCP Server 0.4.0\n\n"
                         "Usage:\n"
                         "  URK_UnityRuntimeExplorer_McpServer.exe [--game-pid <pid>] [--allow-tracing] [--allow-invocation]\n\n"
                         "Options:\n"
                         "  --game-pid <pid>  Select one Explorer-enabled game.\n"
                         "  --allow-tracing   Deprecated compatibility option; permissions live in Explorer Config.\n"
                         "  --allow-invocation Deprecated compatibility option; permissions live in Explorer Config.\n"
                         "  --help            Show this help.\n";
            return 0;
        }
        if (argument == "--allow-tracing") {
            allow_tracing = true;
            continue;
        }
        if (argument == "--allow-invocation") {
            allow_invocation = true;
            continue;
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
    std::cerr << "UnityRuntimeExplorer MCP Server 0.4.0\n"
                 "Transport : stdio\n"
              << "Target    : " << (game_pid ? "game PID " + std::to_string(*game_pid) : "automatic discovery") << '\n'
              << "Mode      : Explorer-controlled permissions\n\n"
                 "This helper is normally started by Codex, Claude, ChatGPT, or another MCP client.\n"
                 "Keep the Explorer-enabled game running. MCP protocol output is reserved for stdout.\n"
                 "Configure discovery, properties, writes, tracing, invocation, and destructive operations in Explorer > Config.\n"
              << "Waiting for MCP messages on stdin...\n" << std::flush;
    Explorer::Mcp::StdioServer server(game_pid, allow_tracing, allow_invocation);
    return server.run();
}
