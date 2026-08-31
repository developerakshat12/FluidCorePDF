#pragma once

#include "FluidCoreAPI.h"

#include <string>

namespace FluidCore {

enum class ArrowStyle {
    SharpTriangle = 0,
    OpenChevron = 1,
    None = 2
};

enum class EdgeDirection {
    Forward = 0,       // Source -> Target (A ──▶ B)
    Bidirectional = 1  // Both Directions (A ◀──▶ B)
};

// Represents a directed or bidirectional link between two workspace nodes.
// Simple graph invariant: at most one edge exists between any pair {A, B}.
struct GraphEdge {
    std::string id;
    std::string sourceNodeId;
    std::string targetNodeId;
    EdgeDirection direction = EdgeDirection::Forward;
    Color color{30, 144, 255, 255}; // Default DodgerBlue
    double strokeWidth = 2.0;
    ArrowStyle arrowStyle = ArrowStyle::SharpTriangle;
    std::string label;
};

} // namespace FluidCore
