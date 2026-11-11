#pragma once

#include <array>
#include <vector>

#include "math/vector.h"

namespace rad
{
    // indexed triangle mesh used by the slow studio shadow mode
    struct simplified_mesh
    {
        std::vector<math::vec3v> positions;
        std::vector<std::array<int, 3>> triangles;
        std::vector<math::vec3v> original_positions;
        int source_vertices = 0;
    };

    // melax progressive mesh reduction with the reference's vertex budget:
    // models up to 400 vertices are left alone, then 85%, 55% and 15% of the
    // original count is kept. the output is a closed subset of the input
    // connectivity; degenerate triangles are dropped.
    simplified_mesh simplify_shadow_mesh(const std::vector<math::vec3v> &positions,
                                         const std::vector<std::array<int, 3>> &triangles);

    // vertices kept for a model with the given original vertex count
    int simplified_vertex_target(int source_vertices);
}
