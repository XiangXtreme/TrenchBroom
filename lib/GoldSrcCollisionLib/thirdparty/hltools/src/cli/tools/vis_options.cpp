#include "vis_tool.h"

#include <cstdlib>

#include "../../common/log.h"

namespace tools
{
vis::vis_options parse_vis_options(const cli::args &args, const std::string &portal_path)
{
    vis::vis_options options;
    options.portal_path = portal_path;
    options.fast = args.has("-fast") || args.has("-previewfastvis");
    // full vis is the default: it culls best, and on portal-dense maps
    // the tighter source clipping prunes the flow recursion so hard that
    // it is also faster than normal vis; -fast stays the iteration mode
    options.full = !args.has("-nofull") && !options.fast;
    options.nofixprt = args.has("-nofixprt");
    options.chart = true;
    options.maxdistance = (unsigned)std::abs(args.int_value("-maxdistance", 0));

    logging::setting("full vis", options.full ? "on" : "off", "on", !options.full);
    logging::setting("fast vis", options.fast ? "on" : "off", "off", options.fast);
    return options;
}

} // namespace tools
