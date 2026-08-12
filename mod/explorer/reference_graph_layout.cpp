// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "reference_graph_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Explorer::ReferenceGraphLayout {
namespace {
float neighbour_barycenter(std::size_t node, const std::vector<Edge>& edges,
                           const std::vector<float>& order, bool predecessors) {
    float total = 0.0f;
    std::size_t count = 0;
    for (const Edge& edge : edges) {
        if ((predecessors && edge.to == node) || (!predecessors && edge.from == node)) {
            const std::size_t neighbour = predecessors ? edge.from : edge.to;
            if (neighbour < order.size()) {
                total += order[neighbour];
                ++count;
            }
        }
    }
    return count == 0 ? std::numeric_limits<float>::quiet_NaN() : total / static_cast<float>(count);
}
} // namespace

void arrange(std::vector<NodePosition>& nodes, const std::vector<Edge>& edges,
             float column_spacing, float row_spacing) {
    if (nodes.empty())
        return;
    std::size_t maximum_depth = 0;
    for (const NodePosition& node : nodes)
        maximum_depth = std::max(maximum_depth, node.depth);
    std::vector<std::vector<std::size_t>> layers(maximum_depth + 1);
    for (std::size_t index = 0; index < nodes.size(); ++index)
        layers[nodes[index].depth].push_back(index);

    std::vector<float> order(nodes.size(), 0.0f);
    const auto refresh_order = [&] {
        for (const auto& layer : layers)
            for (std::size_t row = 0; row < layer.size(); ++row)
                order[layer[row]] = static_cast<float>(row);
    };
    refresh_order();
    // Alternating barycentric sweeps reduce crossings between adjacent layers.
    for (int iteration = 0; iteration < 4; ++iteration) {
        for (std::size_t depth = 1; depth < layers.size(); ++depth) {
            std::stable_sort(layers[depth].begin(), layers[depth].end(), [&](std::size_t left, std::size_t right) {
                const float a = neighbour_barycenter(left, edges, order, true);
                const float b = neighbour_barycenter(right, edges, order, true);
                if (std::isnan(a)) return false;
                if (std::isnan(b)) return true;
                return a < b;
            });
            refresh_order();
        }
        for (std::size_t depth = layers.size(); depth-- > 1;) {
            std::stable_sort(layers[depth - 1].begin(), layers[depth - 1].end(),
                             [&](std::size_t left, std::size_t right) {
                const float a = neighbour_barycenter(left, edges, order, false);
                const float b = neighbour_barycenter(right, edges, order, false);
                if (std::isnan(a)) return false;
                if (std::isnan(b)) return true;
                return a < b;
            });
            refresh_order();
        }
    }

    std::size_t widest_layer = 1;
    for (const auto& layer : layers)
        widest_layer = std::max(widest_layer, layer.size());
    for (std::size_t depth = 0; depth < layers.size(); ++depth) {
        const float center_offset = static_cast<float>(widest_layer - layers[depth].size()) * 0.5f;
        for (std::size_t row = 0; row < layers[depth].size(); ++row) {
            NodePosition& node = nodes[layers[depth][row]];
            node.x = static_cast<float>(depth) * column_spacing;
            node.y = (center_offset + static_cast<float>(row)) * row_spacing;
        }
    }
}
} // namespace Explorer::ReferenceGraphLayout
