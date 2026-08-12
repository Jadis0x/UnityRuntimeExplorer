// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "bounded_stdio_transport.h"

#include <istream>
#include <ostream>

namespace Explorer::Mcp {

BoundedStdioTransport::BoundedStdioTransport(std::istream& input, std::ostream& output,
                                             std::size_t maximum_input_bytes,
                                             std::size_t maximum_output_bytes)
    : input_(input), output_(output), maximum_input_bytes_(maximum_input_bytes),
      maximum_output_bytes_(maximum_output_bytes) {}

BoundedStdioTransport::ReadResult BoundedStdioTransport::read(std::string& line) {
    line.clear();
    bool too_large = false;
    for (;;) {
        const int value = input_.get();
        if (value == std::char_traits<char>::eof()) {
            if (input_.bad())
                return ReadResult::Error;
            if (line.empty() && !too_large)
                return ReadResult::End;
            return too_large ? ReadResult::TooLarge : ReadResult::Message;
        }
        if (value == '\n')
            return too_large ? ReadResult::TooLarge : ReadResult::Message;
        if (value == '\r')
            continue;
        if (line.size() < maximum_input_bytes_)
            line.push_back(static_cast<char>(value));
        else
            too_large = true;
    }
}

bool BoundedStdioTransport::emit(const nlohmann::json& message) {
    std::string serialized = message.dump();
    if (serialized.size() > maximum_output_bytes_) {
        const nlohmann::json id = message.is_object() && message.contains("id")
            ? message["id"] : nlohmann::json(nullptr);
        serialized = nlohmann::json{{"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", -32603}, {"message", "MCP response exceeded the output limit"}}}}.dump();
    }
    std::lock_guard lock(output_mutex_);
    output_ << serialized << '\n' << std::flush;
    return output_.good();
}

} // namespace Explorer::Mcp
