// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "pipe_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Explorer::Mcp {
namespace {
struct Discovery {
    std::uint32_t pid = 0;
    std::wstring pipe;
    CapabilityMask capabilities = 0;
};

std::filesystem::path discovery_directory() {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
        return {};
    return std::filesystem::path(value.data(), value.data() + length) / L"URK" /
           L"UnityRuntimeExplorer" / L"bridges";
}

std::vector<Discovery> discoveries() {
    std::vector<Discovery> result;
    const std::filesystem::path directory = discovery_directory();
    std::error_code error;
    if (directory.empty() || !std::filesystem::exists(directory, error))
        return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != L".json")
            continue;
        try {
            std::ifstream input(entry.path());
            const nlohmann::json value = nlohmann::json::parse(input);
            if (value.value("protocol", std::string{}) != bridge_protocol_version ||
                !value.contains("pid") || !value.contains("pipe"))
                continue;
            const std::string pipe = value["pipe"].get<std::string>();
            result.push_back({value["pid"].get<std::uint32_t>(), std::wstring(pipe.begin(), pipe.end()),
                              value.value("capabilities", CapabilityMask{0})});
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }
    return result;
}

// Liveness without side effects. Opening a candidate used to occupy the
// bridge's single pipe instance and register as a client session, so merely
// enumerating bridges disturbed the one already in use.
bool pipe_is_live(const std::wstring& pipe) {
    if (WaitNamedPipeW(pipe.c_str(), 200))
        return true;
    // A busy instance still proves the bridge is alive.
    return GetLastError() == ERROR_SEM_TIMEOUT;
}

HANDLE open_pipe(const std::wstring& pipe) {
    if (!WaitNamedPipeW(pipe.c_str(), 1000) && GetLastError() != ERROR_SEM_TIMEOUT)
        return INVALID_HANDLE_VALUE;
    return CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
}

bool write_line(HANDLE pipe, std::string text, std::string& error) {
    text.push_back('\n');
    std::size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!WriteFile(pipe, text.data() + offset, static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
            written == 0) {
            error = "MCP bridge write failed (Win32 " + std::to_string(GetLastError()) + ")";
            return false;
        }
        offset += written;
    }
    return true;
}

// Newline framing over a byte-mode pipe. Whatever a read delivers past the
// newline is kept in the carry buffer; discarding it used to lose any response
// the bridge had already coalesced into the same transfer.
bool read_line(HANDLE pipe, std::string& carry, std::string& line, std::string& error) {
    line.clear();
    for (;;) {
        if (const std::size_t newline = carry.find('\n'); newline != std::string::npos) {
            line.assign(carry, 0, newline);
            carry.erase(0, newline + 1);
            return true;
        }
        if (carry.size() > max_message_bytes) {
            error = "MCP bridge response exceeded the message limit";
            return false;
        }
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
            error = "MCP bridge read failed (Win32 " + std::to_string(GetLastError()) + ")";
            return false;
        }
        carry.append(buffer.data(), read);
    }
}
} // namespace

PipeClient::~PipeClient() {
    close();
}

CapabilityMask PipeClient::capabilities() const {
    if (game_pid_ == 0)
        return 0;
    for (const Discovery& candidate : discoveries())
        if (candidate.pid == game_pid_)
            return candidate.capabilities;
    return 0;
}

bool PipeClient::connect(std::optional<std::uint32_t> game_pid, std::string& error) {
    close();
    error.clear();
    std::vector<Discovery> candidates = discoveries();
    if (game_pid)
        std::erase_if(candidates, [&](const Discovery& candidate) { return candidate.pid != *game_pid; });
    std::erase_if(candidates, [](const Discovery& candidate) { return !pipe_is_live(candidate.pipe); });

    if (candidates.size() > 1) {
        error = "Multiple live UnityRuntimeExplorer bridges were found; start the helper with --game-pid <pid>.";
        return false;
    }
    if (candidates.size() == 1) {
        pipe_ = open_pipe(candidates.front().pipe);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            game_pid_ = candidates.front().pid;
            carry_.clear();
            return true;
        }
    }
    error = game_pid ? "No live UnityRuntimeExplorer bridge was found for PID " + std::to_string(*game_pid) + "."
                     : "No live UnityRuntimeExplorer bridge was found. Start a game with the Explorer DLL loaded.";
    return false;
}

bool PipeClient::transact(const Request& request, Response& response, std::string& error) {
    if (pipe_ == INVALID_HANDLE_VALUE) {
        error = "MCP bridge is not connected";
        return false;
    }
    if (!write_line(pipe_, serialize(request), error)) {
        close();
        return false;
    }
    std::string line;
    if (!read_line(pipe_, carry_, line, error)) {
        close();
        return false;
    }
    if (!parse_response(line, response, error)) {
        close();
        return false;
    }
    if (response.id != request.id) {
        error = "MCP bridge response id did not match the request";
        close();
        return false;
    }
    return true;
}

void PipeClient::close() {
    if (pipe_ != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
    game_pid_ = 0;
    carry_.clear();
}

} // namespace Explorer::Mcp
