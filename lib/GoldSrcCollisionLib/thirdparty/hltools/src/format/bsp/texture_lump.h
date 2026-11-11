#pragma once

#include <string>
#include <vector>

#include "data.h"
#include "format/miptex/texture.h"

namespace format
{
    // read embedded miptex payloads and external texture names from a bsp
    // texture lump
    bool collect_bsp_textures(const map_data &map, std::vector<mip_texture> &textures,
                              std::vector<std::string> *external = nullptr,
                              std::string *error = nullptr);

    // names in lump order, including entries whose pixel data is external. an
    // entry with a negative offset yields an empty name.
    std::vector<std::string> bsp_texture_names(const std::vector<byte> &lump);

    // build a dmiptexlump_t directory followed by complete miptex payloads.
    // offsets are stored relative to the miptex header, matching the engine and
    // collect_bsp_textures.
    std::vector<byte> build_texture_lump(const std::vector<mip_texture> &textures);
}
