#pragma once

#include "brush.h"

namespace csg
{
    void merge_legacy_clip_brushes(const map_source &map, built_entity &entity,
                                  plane_store &planes, const brush_build_options &options);
}
