#pragma once

#include <array>
#include <vector>

#include "internal.h"

namespace rad
{
    // build the slow mode's expanded facet planes for a simplified mesh
    void build_studio_facets(studio_shadow_model &model,
                             const std::vector<vec3v> &positions,
                             const std::vector<std::array<int, 3>> &triangles);

    // reference ClipRayToFacet for a point sized trace hull
    bool clip_segment_to_facet(const studio_shadow_model &model,
                               const studio_shadow_facet &facet, const vec3v &start,
                               const vec3v &end);
}
