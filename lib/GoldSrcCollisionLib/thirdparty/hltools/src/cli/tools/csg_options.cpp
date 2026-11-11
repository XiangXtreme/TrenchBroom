#include "csg_tool.h"

#include "../../common/error.h"
#include "../../common/log.h"

namespace tools
{
namespace
{
csg::clip_type clip_type_arg(const cli::args &args)
{
    csg::clip_type clip = csg::clip_type::simple;
    const char *value = args.value("-cliptype", nullptr);
    if (value && !csg::parse_clip_type(value, clip))
        err::fatal("-cliptype must be smallest, normalized, simple, precise, legacy, or legacytrim");
    return clip;
}
} // namespace

csg::csg_options parse_csg_options(const cli::args &args, const std::string &map_path)
{
    csg::csg_options options;
    options.brush.noclip = args.has("-noclip");
    options.brush.only_entities = args.has("-onlyents");
    options.brush.nullify_trigger = args.has("-nullifytrigger");
    options.brush.merge_legacy_clip = args.has("-clipmerge");
    options.brush.world_extent = (vec_t)args.float_value("-worldextent", options.brush.world_extent);
    options.brush.global_scale = (vec_t)args.float_value("-scale", options.brush.global_scale);
    options.brush.clip = clip_type_arg(args);
    options.brush_union_threshold = (vec_t)args.float_value("-brushunion", options.brush_union_threshold);
    options.wad.map_path = map_path;
    options.wad.wad_textures = args.has("-wadtextures");
    options.wad.wad_auto_detect = !args.has("-nowadautodetect");
    for (const auto &include : args.values("-wadinclude"))
        options.wad.wad_include.push_back(include);
    if (const char *wad_cfg_file = args.value("-wadcfgfile", nullptr))
        options.wad.wad_cfg_file = wad_cfg_file;
    if (const char *wad_config = args.value("-wadconfig", nullptr))
        options.wad.wad_config_name = wad_config;
    if (const char *hull_file = args.value("-hullfile", nullptr))
        options.hull_file_path = hull_file;
    options.sky_clip = !args.has("-noskyclip");
    if (const char *null_file = args.value("-nullfile", nullptr))
        options.invisible_items = csg::load_invisible_items(null_file);

    logging::setting("wadtextures", options.wad.wad_textures ? "on" : "off", "off", options.wad.wad_textures);

    // embedding drops every wad from the runtime list, so the options that
    // only shape that list have nothing left to act on. -wadcfgfile and
    // -wadconfig still matter: they say which wads the textures are read
    // from in the first place.
    if (!options.wad.wad_textures)
    {
        if (args.has("-nowadautodetect"))
            logging::warn("-nowadautodetect has no effect without -wadtextures: "
                          "embedded textures leave no wad list to prune");
        if (!args.values("-wadinclude").empty())
            logging::warn("-wadinclude has no effect without -wadtextures: "
                          "every used texture is already embedded");
    }
    logging::setting("clip type", csg::clip_type_name(options.brush.clip), "simple", args.has("-cliptype"));
    logging::setting("legacy clip merge", options.brush.merge_legacy_clip ? "on" : "off", "off",
                     options.brush.merge_legacy_clip);
    return options;
}

} // namespace tools
