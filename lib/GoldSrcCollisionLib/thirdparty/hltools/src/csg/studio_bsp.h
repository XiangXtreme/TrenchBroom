#pragma once

#include <string>
#include <vector>

#include "format/miptex/texture.h"
#include "map_parser.h"

namespace csg
{
    // Bakes every point entity carrying zhlt_studiobsp into world space
    // brushes. The entity becomes a func_wall (or its brushes join worldspawn
    // and the marker becomes info_null when zhlt_studiobsp_world is set).
    // Returns the model skins the miptex lump has to embed.
    std::vector<format::mip_texture> inject_studio_brushes(map_source &map,
                                                           const std::string &map_path);
}
