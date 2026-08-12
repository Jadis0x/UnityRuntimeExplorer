// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <cstddef>
#include <vector>

namespace Explorer::ReferenceGraphLayout {
struct NodePosition {
    std::size_t depth = 0;
    float x = 0.0f;
    float y = 0.0f;
};
struct Edge {
    std::size_t from = 0;
    std::size_t to = 0;
};

void arrange(std::vector<NodePosition>& nodes, const std::vector<Edge>& edges,
             float column_spacing = 300.0f, float row_spacing = 94.0f);
} // namespace Explorer::ReferenceGraphLayout
