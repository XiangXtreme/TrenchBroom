#include "rad_tool.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

#include "../../common/error.h"
#include "../../common/string_util.h"
#include "../../common/filesystem.h"
#include "../../common/log.h"

namespace stdfs = std::filesystem;

namespace tools
{
namespace
{
// resolves the texlight file cascade: lightsrad from the map folder,
// tool folder or working directory; then <mapname>rad; then the
// -lights file through its own fallback chain
void resolve_rad_files(const std::string &mapdir, const std::string &mapfile, const std::string &appdir,
                       const char *user_rad, std::vector<std::string> &out, std::string &generic_file)
{
    std::string path;
    std::string generic_rad;

    path = mapdir + "lights.rad";
    if (fs::exists(path))
    {
        out.push_back(path);
        generic_rad = path;
    }
    else
    {
        path = appdir + "lights.rad";
        if (fs::exists(path))
        {
            out.push_back(path);
            generic_rad = path;
        }
        else
        {
            path = "lights.rad";
            if (fs::exists(path))
            {
                out.push_back(path);
                generic_rad = path;
            }
        }
    }

    path = mapdir + mapfile + ".rad";
    if (fs::exists(path))
    {
        out.push_back(path);
    }
    generic_file = generic_rad;

    if (user_rad)
    {
        std::string userfile = user_rad;
        size_t slash = userfile.find_last_of("/\\:");
        if (slash != std::string::npos)
            userfile = userfile.substr(slash + 1);

        auto with_rad = [](const std::string &name) {
            size_t sl = name.find_last_of("/\\");
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos && (sl == std::string::npos || dot > sl))
                return name;
            return name + ".rad";
        };

        path = user_rad;
        if (fs::exists(path))
        {
            out.push_back(path);
        }
        else
        {
            path = with_rad(user_rad);
            if (fs::exists(path))
            {
                out.push_back(path);
            }
            else
            {
                path = with_rad(mapdir + userfile);
                if (fs::exists(path))
                {
                    out.push_back(path);
                }
                else
                {
                    path = with_rad(appdir + userfile);
                    if (fs::exists(path))
                    {
                        out.push_back(path);
                    }
                    else
                    {
                        path = with_rad(userfile);
                        if (fs::exists(path))
                        {
                            out.push_back(path);
                        }
                    }
                }
            }
        }
    }
}
} // namespace

rad::rad_options parse_rad_options(const cli::args &args, int argc, char **argv, const std::string &base)
{
    rad::rad_options options;
    options.fastmode = args.has("-fast");
    options.dumppatches = args.has("-dump");
    options.dumpgather = args.has("-dumpgather");
    options.gpu = args.has("-gpu");
    if (args.has("-extra"))
    {
        options.extra = true;
        if (options.numbounce < 12)
        {
            options.numbounce = 12;
        }
    }
    const int bounce = args.int_value("-bounce", (int)options.numbounce);
    if (bounce < 0)
        err::fatal("-bounce must not be negative");
    options.numbounce = (unsigned)bounce;
    options.lerp_enabled = !args.has("-nolerp");
    options.chop = (float)args.float_value("-chop", options.chop);
    options.texchop = (float)args.float_value("-texchop", options.texchop);
    options.texscale = !args.has("-notexscale");
    options.subdivide = !args.has("-nosubdivide");
    if (args.has("-scale"))
    {
        // munge the monochrome lightscale into the colour one
        float scale = (float)args.float_value("-scale", 2.0);
        options.colour_lightscale = {scale, scale, scale};
    }
    options.fade = (float)args.float_value("-fade", options.fade);
    if (options.fade < 0.0)
        err::fatal("-fade must be a positive number");
    options.limitthreshold = (float)args.float_value("-limiter", options.limitthreshold);
    options.drawoverload = args.has("-drawoverload");
    options.circus = args.has("-circus");
    options.sky_lighting_fix = !args.has("-noskyfix");
    options.incremental = args.has("-incremental");
    if (args.has("-gamma"))
    {
        float gamma = (float)args.float_value("-gamma", 0.55);
        options.colour_qgamma = {gamma, gamma, gamma};
    }
    options.dlight_threshold = (float)args.float_value("-dlight", options.dlight_threshold);
    options.indirect_sun = (float)args.float_value("-sky", options.indirect_sun);
    options.smoothing_value = (float)args.float_value("-smooth", options.smoothing_value);
    options.smoothing_value_2 = (float)args.float_value("-smooth2", options.smoothing_value_2);
    options.coring = (float)args.float_value("-coring", options.coring);
    const auto size_in_bytes = [&](const char *flag, int default_bytes) {
        const int kb = args.int_value(flag, default_bytes / 1024);
        if (kb < 0 || kb > std::numeric_limits<int>::max() / 1024)
            err::fatal("%s must be between 0 and %d KiB", flag,
                       std::numeric_limits<int>::max() / 1024);
        return kb * 1024;
    };
    options.max_map_lightdata = size_in_bytes("-lightdata", options.max_map_lightdata);
    options.max_map_miptex = size_in_bytes("-texdata", options.max_map_miptex);
    if (args.has("-vismatrix"))
    {
        const char *value = args.value("-vismatrix", "");
        if (str::iequals(value, "normal"))
            options.method = rad::vis_method::vismatrix;
        else if (str::iequals(value, "sparse"))
            options.method = rad::vis_method::sparse_vismatrix;
        else if (str::iequals(value, "off"))
            options.method = rad::vis_method::no_vismatrix;
        else
            err::fatal("Unknown vismatrix type: '%s'", value);
    }
    options.allow_spread = !args.has("-nospread");
    options.allow_opaques = !(args.has("-nopaque") || args.has("-noopaque"));
    options.direct_scale = (float)args.float_value("-dscale", options.direct_scale);
    options.customshadow_with_bouncelight = args.has("-customshadowwithbounce");
    options.rgb_transfers = args.has("-rgbtransfers");
    options.minlight = (unsigned char)std::max(0, std::min(args.int_value("-minlight", options.minlight), 255));
    options.softsky = args.int_value("-softsky", options.softsky ? 1 : 0) != 0;
    options.studioshadow = !args.has("-nostudioshadow");
    options.drawpatch = args.has("-drawpatch");
    options.drawedge = args.has("-drawedge");
    options.drawlerp = args.has("-drawlerp");
    options.drawnudge = args.has("-drawnudge");
    {
        int compress = args.int_value("-compress", (int)options.transfer_compress_type);
        if (compress < 0 || compress > 2)
            err::fatal("invalid value for '-compress'");
        options.transfer_compress_type = (rad::float_format)compress;
        int rgbcompress = args.int_value("-rgbcompress", (int)options.rgbtransfer_compress_type);
        if (rgbcompress < 0 || rgbcompress > 3)
            err::fatal("invalid value for '-rgbcompress'");
        options.rgbtransfer_compress_type = (rad::vector_format)rgbcompress;
    }
    options.translucentdepth = (float)args.float_value("-depth", options.translucentdepth);
    options.blockopaque = args.int_value("-blockopaque", options.blockopaque);
    options.notextures = args.has("-notextures");
    options.texreflectgamma = (float)args.float_value("-texreflectgamma", options.texreflectgamma);
    options.texreflectscale = (float)args.float_value("-texreflectscale", options.texreflectscale);
    options.blur = (float)args.float_value("-blur", options.blur);
    const auto bounded_float = [&](const char *flag, float fallback, double min, double max) {
        if (!args.has(flag))
            return fallback;
        const char *value = args.value(flag, nullptr);
        double parsed = 0;
        if (!value || !cli::option_float("rad", flag, value, min, max, parsed))
            err::fatal("invalid value for %s", flag);
        return (float)parsed;
    };
    options.blurclamp = bounded_float("-blurclamp", options.blurclamp, 0.0, 1.0);
    options.pcf = args.int_value("-pcf", options.pcf);
    if (options.pcf < 1 || options.pcf > 8)
        err::fatal("-pcf must be between 1 and 8");
    options.ao = args.has("-ao");
    options.ao_stats = args.has("-aostats");
    options.light_cull = args.has("-lightcull");
    if (args.has("-aostudio"))
    {
        const char *mode = args.value("-aostudio", "");
        if (str::iequals(mode, "0") || str::iequals(mode, "inherit"))
            options.ao_studio_mode = -1;
        else if (str::iequals(mode, "1") || str::iequals(mode, "fast"))
            options.ao_studio_mode = 0;
        else if (str::iequals(mode, "2") || str::iequals(mode, "normal"))
            options.ao_studio_mode = 1;
        else if (str::iequals(mode, "3") || str::iequals(mode, "slow"))
            options.ao_studio_mode = 2;
        else
            err::fatal("-aostudio must be inherit/0, fast/1, normal/2 or slow/3");
    }
    options.ao_scale = bounded_float("-aoscale", options.ao_scale, 1.0, 1024.0);
    options.ao_gain = bounded_float("-aogain", options.ao_gain, 0.125, 8.0);
    options.ao_level = args.int_value("-aolevel", options.ao_level);
    if (options.ao_level < 1 || options.ao_level > 8)
        err::fatal("-aolevel must be between 1 and 8");
    options.ao_minweight = bounded_float("-aominweight", options.ao_minweight, 0.0, 0.1);
    options.ao_opacity = bounded_float("-aoopacity", options.ao_opacity, 0.0, 8.0);
    if (args.has("-aomode"))
    {
        const char *mode = args.value("-aomode", "");
        if (str::iequals(mode, "0") || str::iequals(mode, "normal"))
            options.ao_mode = 0;
        else if (str::iequals(mode, "1") || str::iequals(mode, "additive"))
            options.ao_mode = 1;
        else
            err::fatal("-aomode must be normal/0 or additive/1");
    }
    options.noemitterrange = args.has("-noemitterrange");
    options.bleedfix = !args.has("-nobleedfix");
    options.texlightgap = (float)args.float_value("-texlightgap", options.texlightgap);
    if (args.has("-pre25")) // overrides -limiter
    {
        options.pre25update = true;
        options.limitthreshold = 188.0f;
    }

    // repeatable and multi argument flags need a raw scan
    const char *user_lights = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (str::iequals(argv[i], "-waddir") && i + 1 < argc)
        {
            options.wad_folders.push_back(argv[i + 1]);
        }
        if (str::iequals(argv[i], "-lights") && i + 1 < argc)
        {
            user_lights = argv[i + 1];
        }
        if (str::iequals(argv[i], "-ambient") && i + 3 < argc)
        {
            options.ambient[0] = (float)atof(argv[i + 1]) * 128;
            options.ambient[1] = (float)atof(argv[i + 2]) * 128;
            options.ambient[2] = (float)atof(argv[i + 3]) * 128;
        }
        if (str::iequals(argv[i], "-aocolor"))
        {
            if (i + 3 >= argc)
                err::fatal("-aocolor requires three values");
            for (int channel = 0; channel < 3; ++channel)
            {
                double value = 0;
                if (!cli::option_float("rad", "-aocolor", argv[i + 1 + channel], 0.0, 65535.0, value))
                    err::fatal("invalid -aocolor channel");
                options.ao_color[channel] = (float)value;
            }
        }
        if (str::iequals(argv[i], "-colourgamma") && i + 3 < argc)
        {
            options.colour_qgamma[0] = (float)atof(argv[i + 1]);
            options.colour_qgamma[1] = (float)atof(argv[i + 2]);
            options.colour_qgamma[2] = (float)atof(argv[i + 3]);
        }
        if (str::iequals(argv[i], "-colourscale") && i + 3 < argc)
        {
            options.colour_lightscale[0] = (float)atof(argv[i + 1]);
            options.colour_lightscale[1] = (float)atof(argv[i + 2]);
            options.colour_lightscale[2] = (float)atof(argv[i + 3]);
        }
        if (str::iequals(argv[i], "-colourjitter") && i + 3 < argc)
        {
            options.colour_jitter_hack[0] = (float)atof(argv[i + 1]);
            options.colour_jitter_hack[1] = (float)atof(argv[i + 2]);
            options.colour_jitter_hack[2] = (float)atof(argv[i + 3]);
        }
        if (str::iequals(argv[i], "-jitter") && i + 3 < argc)
        {
            options.jitter_hack[0] = (float)atof(argv[i + 1]);
            options.jitter_hack[1] = (float)atof(argv[i + 2]);
            options.jitter_hack[2] = (float)atof(argv[i + 3]);
        }
        if (str::iequals(argv[i], "-drawsample") && i + 4 < argc)
        {
            options.drawsample = true;
            options.drawsample_origin[0] = (float)atof(argv[i + 1]);
            options.drawsample_origin[1] = (float)atof(argv[i + 2]);
            options.drawsample_origin[2] = (float)atof(argv[i + 3]);
            options.drawsample_radius = (float)atof(argv[i + 4]);
        }
    }

    logging::setting("oversampling (-extra)", options.extra ? "on" : "off", "off", options.extra);
    logging::setting("bounces", std::to_string(options.numbounce).c_str(), "8", options.numbounce != 8);
    logging::setting("gpu lighting", options.gpu ? "on (approximate)" : "off", "off", options.gpu);
    if (options.gpu)
        logging::setting("gpu device", rad::gpu_device_description().c_str(), "", true);

    // resolve the lightsrad cascade
    {
        std::string mapdir = fs::directory(base);
        if (!mapdir.empty() && mapdir.back() != '/' && mapdir.back() != '\\')
            mapdir += stdfs::path::preferred_separator;
        std::string appdir = fs::directory(argv[0]);
        if (!appdir.empty() && appdir.back() != '/' && appdir.back() != '\\')
            appdir += stdfs::path::preferred_separator;
        std::string mapfile = base;
        size_t slash = mapfile.find_last_of("/\\:");
        if (slash != std::string::npos)
            mapfile = mapfile.substr(slash + 1);
        resolve_rad_files(mapdir, mapfile, appdir, user_lights, options.rad_files, options.generic_rad_file);
    }

    return options;
}

} // namespace tools
