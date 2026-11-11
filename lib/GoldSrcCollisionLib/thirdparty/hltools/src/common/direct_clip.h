#pragma once

#include <vector>

#include "types.h"
#include "../math/vector.h"

// A small, format-independent representation of a collision BSP produced by
// the Python surface pipeline.  It deliberately does not use bsp's intrusive
// node types: the CSG stage builds this value and the BSP stage installs it in
// the final GoldSrc lumps after normal writer optimization has completed.
namespace direct_clip
{
    struct plane
    {
        math::vec3v normal;
        vec_t dist = 0;
    };

    struct node
    {
        int plane = -1;
        int front = -1;
        int back = -1;
    };

    struct tree
    {
        std::vector<plane> planes;
        std::vector<node> nodes;
        int head = -1;
        math::vec3v mins;
        math::vec3v maxs;
        bool valid = false;
    };

    struct model
    {
        tree hulls[4];
        bool valid = false;
    };
}
