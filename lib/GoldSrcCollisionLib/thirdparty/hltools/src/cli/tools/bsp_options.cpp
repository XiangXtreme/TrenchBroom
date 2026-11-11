#include "bsp_tool.h"

namespace tools
{
bsp::bsp_options parse_bsp_options(const cli::args &args)
{
    bsp::bsp_options options;
    options.nofill = args.has("-nofill");
    options.noinsidefill = args.has("-noinsidefill");
    options.notjunc = args.has("-notjunc");
    options.nobrink = args.has("-nobrink");
    options.noclip = args.has("-noclip");
    options.noopt = args.has("-noopt");
    options.noclipnodemerge = args.has("-noclipnodemerge");
    options.deepclip = args.has("-deepclip");
    options.leakonly = args.has("-leakonly");
    options.allleaks = args.has("-allleaks");
    options.nulltex = !args.has("-nonulltex");
    options.nohull2 = args.has("-nohull2");
    options.maxnode_size = args.int_value("-maxnodesize", options.maxnode_size);
    options.subdivide_size = args.int_value("-subdivide", options.subdivide_size);
    return options;
}

} // namespace tools
