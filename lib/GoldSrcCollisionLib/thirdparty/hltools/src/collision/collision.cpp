#include "collision.h"

#include <mutex>
#include <utility>

#include "../bsp/bsp.h"
#include "../common/error.h"
#include "../csg/bsp_output.h"
#include "../csg/csg.h"
#include "runtime.h"

namespace hltools::collision
{
result compile(std::string map_text, clip_type clip)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    detail::take_warnings();
    if (clip < clip_type::smallest || clip > clip_type::legacytrim)
        err::fatal("invalid collision cliptype");

    csg::csg_options options;
    options.brush.clip =
        static_cast<csg::clip_type>(static_cast<int>(clip) + 1);
    auto csg = csg::run_csg(csg::parse_map_text(std::move(map_text)), options);
    result output;
    if (csg.planes.empty())
    {
        format::dmodel_t model{};
        for (auto &head : model.headnode)
            head = -1;
        output.bsp.models.push_back(model);
    }
    else
    {
        auto input = csg::build_intermediate_data(csg, options.brush);
        output.bsp = csg::build_bsp_data(csg);
        bsp::bsp_input bsp_input;
        for (int h = 0; h < 4; ++h)
        {
            bsp_input.surfaces[h] = std::move(input.surfaces[h]);
            bsp_input.brushes[h] = std::move(input.brushes[h]);
        }
        bsp_input.hull_sizes = std::move(input.hull_sizes);
        bsp_input.planes = std::move(input.planes);
        bsp_input.surface_clips = std::move(input.surface_clips);
        bsp::bsp_options bsp_options;
        bsp_options.nofill = true;
        // Texture pixels do not affect collision; there is no miptex lump
        // to optimize in this build. All geometry/clipnode passes remain on.
        bsp_options.noopt = true;
        bsp::run_bsp(output.bsp, {}, bsp_options, std::move(bsp_input));
    }
    output.warnings = detail::take_warnings();
    return output;
}
} // namespace hltools::collision
