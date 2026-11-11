#pragma once

#include <string>
#include <vector>

#include "../format/bsp/data.h"

// Embedded CSG/BSP entry point. The CLI and this target compile the same
// geometry sources; only asset loading and process/file output are omitted.
namespace hltools::collision
{
enum class clip_type
{
    smallest,
    normalized,
    simple,
    precise,
    legacy,
    legacytrim
};

struct stage_timings
{
    double parse_milliseconds = 0;
    double csg_milliseconds = 0;
    double intermediate_milliseconds = 0;
    double bsp_data_milliseconds = 0;
    double bsp_milliseconds = 0;
};

struct result
{
    format::map_data bsp;
    std::vector<std::string> warnings;
    stage_timings timings;
};

// Compiles GoldSrc MAP text without reading WADs or writing sidecars. Uses
// -nofill because live editor maps need not be sealed. Fatal stage errors
// throw std::runtime_error, including errors in legacytrim async workers.
// Calls are serialized: BSP writer/portal helpers have shared scratch data.
result compile(std::string map_text, clip_type clip = clip_type::legacy);
} // namespace hltools::collision
