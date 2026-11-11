#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"
#include "format/mdl/studio_model.h"

namespace format
{
    // which pose a reader builds the vertices from. GoldSrc picks the sequence
    // from the entity's `sequence` key (default 0) and plays frame 0 for
    // compile time use, while `body` selects one model from each bodypart and
    // `skin` selects one skin family.
    struct goldsrc_model_pose
    {
        int sequence = 0;
        int frame = 0;
        int body = 0;
        int skin = 0;
    };

    // reads a goldsrc studio model (IDST version 10) into the engine neutral
    // studio_model. vertices and normals are already posed at the requested
    // sequence frame, so a caller can transform them by the entity placement
    // and use them directly. the returned meshes are triangle lists and each
    // mesh material indexes studio_model::textures.
    //
    // goldsrc stores animation as run length encoded per bone channels; only
    // the requested frame is decoded here, not the whole sequence.
    bool read_goldsrc_model(const byte *data, std::size_t size,
                            const goldsrc_model_pose &pose, studio_model &out,
                            std::string *error = nullptr);

    bool read_goldsrc_model(const std::vector<byte> &data,
                            const goldsrc_model_pose &pose, studio_model &out,
                            std::string *error = nullptr);
}
