#pragma once

#include <string>
#include <vector>

#include "common/types.h"

namespace format
{
    struct map_data;

    struct lightmap_atlas
    {
        unsigned width = 0;
        unsigned height = 0;
        int faces = 0;
        int tiles = 0;
        std::vector<byte> pixels; // top down rgb8
    };

    // where one face's lightmap landed in the atlas, in the order the packer
    // placed it: face then style slot, so a face with n styles owns n entries
    struct lightmap_tile
    {
        int face = 0;
        int style = 0;
        unsigned x = 0;
        unsigned y = 0;
        unsigned width = 0;
        unsigned height = 0;
        int mins[2] = {0, 0};
    };

    // packs every lit face/style into a deterministic, one pixel separated
    // atlas in face then style slot order
    bool build_lightmap_atlas(const map_data &map, lightmap_atlas &atlas,
                              std::string *error = nullptr,
                              std::vector<lightmap_tile> *tiles = nullptr);
}
