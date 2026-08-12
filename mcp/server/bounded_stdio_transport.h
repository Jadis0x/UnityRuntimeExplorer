// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>

namespace Explorer::Mcp {

class BoundedStdioTransport {
  public:
    enum class ReadResult { Message, TooLarge, End, Error };

    BoundedStdioTransport(std::istream& input, std::ostream& output,
                          std::size_t maximum_input_bytes, std::size_t maximum_output_bytes);
    ReadResult read(std::string& line);
    bool emit(const nlohmann::json& message);

  private:
    std::istream& input_;
    std::ostream& output_;
    std::size_t maximum_input_bytes_;
    std::size_t maximum_output_bytes_;
    std::mutex output_mutex_;
};

} // namespace Explorer::Mcp
