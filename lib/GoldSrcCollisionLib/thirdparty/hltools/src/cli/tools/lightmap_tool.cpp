#include "lightmap_tool.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../../common/filesystem.h"
#include "../../common/log.h"
#include "../../common/string_util.h"
#include "format/bmp/file.h"
#include "format/bsp/data.h"
#include "format/bsp/face_extents.h"
#include "format/bsp/file.h"
#include "format/bsp/lightmap_atlas.h"

namespace stdfs = std::filesystem;

namespace tools
{
    namespace
    {
        void print_help()
        {
            logging::console(
                "usage\n"
                "  hltools lightmap <map.bsp> <output.bmp> [-manifest <output.json>] [-force]\n"
                "\n"
                "  exports every lit face and light style to one deterministic 24-bit\n"
                "  rgb atlas for fast visual or pixel-by-pixel comparisons.\n"
                "\n"
                "  -manifest writes where each face landed in the atlas, together with the\n"
                "  face plane, lightmap axes, extents and its lightmap space polygon, so a\n"
                "  consumer can sample the atlas without re-deriving the layout.\n");
        }

        bool is_help(const char *s)
        {
            return str::iequals(s, "-h") || str::iequals(s, "-help")
                || str::iequals(s, "--help") || str::iequals(s, "help");
        }

        bool extension_is(const std::string &path, const char *extension)
        {
            return str::iequals(stdfs::path(path).extension().string().c_str(), extension);
        }

        void write_float(std::FILE *out, double value)
        {
            // fixed precision keeps the file small while staying well inside the
            // engine's own lightmap precision
            std::fprintf(out, "%.6g", value);
        }

        // the face's polygon in lightmap space: one luxel equals 16 world units
        void write_polygon(std::FILE *out, const format::map_data &map,
                           const format::dface_t &face, const format::texinfo_t &tex)
        {
            std::fprintf(out, "[");
            for (int i = 0; i < face.numedges; i++)
            {
                const int e = map.surfedges[(std::size_t)face.firstedge + (std::size_t)i];
                const format::dvertex_t *v = e >= 0
                    ? &map.vertexes[map.edges[(std::size_t)e].v[0]]
                    : &map.vertexes[map.edges[(std::size_t)-e].v[1]];

                const double u =
                    (double)format::calculate_point_vecs_product(v->point, tex.vecs[0]) / 16.0;
                const double t =
                    (double)format::calculate_point_vecs_product(v->point, tex.vecs[1]) / 16.0;

                std::fprintf(out, "%s[", i ? "," : "");
                write_float(out, u);
                std::fprintf(out, ",");
                write_float(out, t);
                std::fprintf(out, "]");
            }
            std::fprintf(out, "]");
        }

        bool write_manifest(const std::string &path, const format::map_data &map,
                            const format::lightmap_atlas &atlas,
                            const std::vector<format::lightmap_tile> &tiles,
                            std::string *error)
        {
            std::FILE *out = std::fopen(path.c_str(), "wb");
            if (!out)
            {
                if (error)
                    *error = "could not write '" + path + "'";
                return false;
            }

            std::fprintf(out,
                         "{\"atlas\":{\"width\":%u,\"height\":%u,\"faces\":%d,"
                         "\"tiles\":%d},\n\"faces\":[",
                         atlas.width, atlas.height, atlas.faces, atlas.tiles);

            bool first = true;
            for (const format::lightmap_tile &tile : tiles)
            {
                // the engine blends styles at runtime; the editor preview only needs
                // the base style
                if (tile.style != 0)
                    continue;

                const format::dface_t &face = map.faces[(std::size_t)tile.face];
                const format::dplane_t &plane = map.planes[(std::size_t)face.planenum];
                const format::texinfo_t &tex =
                    map.texinfo[(std::size_t)format::parse_texinfo_for_face(map, &face)];

                double normal[3] = {plane.normal[0], plane.normal[1], plane.normal[2]};
                double dist = plane.dist;
                if (face.side)
                {
                    normal[0] = -normal[0];
                    normal[1] = -normal[1];
                    normal[2] = -normal[2];
                    dist = -dist;
                }

                std::fprintf(out, "%s\n{\"face\":%d,\"plane\":[", first ? "" : ",",
                             tile.face);
                first = false;
                write_float(out, normal[0]);
                std::fprintf(out, ",");
                write_float(out, normal[1]);
                std::fprintf(out, ",");
                write_float(out, normal[2]);
                std::fprintf(out, "],\"dist\":");
                write_float(out, dist);

                std::fprintf(out, ",\"vecs\":[[");
                for (int k = 0; k < 4; k++)
                {
                    if (k)
                        std::fprintf(out, ",");
                    write_float(out, tex.vecs[0][k]);
                }
                std::fprintf(out, "],[");
                for (int k = 0; k < 4; k++)
                {
                    if (k)
                        std::fprintf(out, ",");
                    write_float(out, tex.vecs[1][k]);
                }
                std::fprintf(out, "]]");

                std::fprintf(out, ",\"texinfo\":%d,\"mins\":[%d,%d],\"size\":[%u,%u],"
                                  "\"tile\":[%u,%u],\"lightofs\":%d,\"styles\":[%d,%d,%d,%d]",
                             face.texinfo, tile.mins[0], tile.mins[1], tile.width,
                             tile.height, tile.x, tile.y, face.lightofs, face.styles[0],
                             face.styles[1], face.styles[2], face.styles[3]);

                std::fprintf(out, ",\"poly\":");
                write_polygon(out, map, face, tex);
                std::fprintf(out, "}");
            }

            std::fprintf(out, "\n]}\n");
            const bool ok = std::ferror(out) == 0;
            std::fclose(out);

            if (!ok && error)
                *error = "could not write '" + path + "'";
            return ok;
        }
    } // namespace

    int run_lightmap_tool(int argc, char **argv)
    {
        if (argc < 2 || is_help(argv[1]))
        {
            print_help();
            return argc >= 2 ? 0 : 1;
        }
        bool force = false;
        std::string manifest_path;
        std::vector<std::string> positional;
        for (int i = 1; i < argc; i++)
        {
            if (str::iequals(argv[i], "-force"))
                force = true;
            else if (str::iequals(argv[i], "-manifest") && i + 1 < argc)
                manifest_path = argv[++i];
            else if (argv[i][0] != '-')
                positional.emplace_back(argv[i]);
        }
        if (positional.size() != 2)
        {
            print_help();
            return 1;
        }
        const std::string &source = positional[0];
        const std::string &destination = positional[1];
        if (!extension_is(source, ".bsp") || !extension_is(destination, ".bmp"))
        {
            logging::console("lightmap: expected a .bsp source and .bmp destination\n");
            return 1;
        }
        if (fs::exists(destination) && !force)
        {
            logging::console("lightmap: '%s' already exists (use -force to overwrite)\n",
                             destination.c_str());
            return 1;
        }
        if (!manifest_path.empty() && fs::exists(manifest_path) && !force)
        {
            logging::console("lightmap: '%s' already exists (use -force to overwrite)\n",
                             manifest_path.c_str());
            return 1;
        }

        format::map_data map;
        if (!format::bsp_file::load(source, map))
        {
            logging::console("lightmap: could not load '%s'\n", source.c_str());
            return 1;
        }
        format::lightmap_atlas atlas;
        std::vector<format::lightmap_tile> tiles;
        std::string error;
        if (!format::build_lightmap_atlas(map, atlas, &error,
                                          manifest_path.empty() ? nullptr : &tiles))
        {
            logging::console("lightmap: %s\n", error.c_str());
            return 1;
        }
        if (!format::write_rgb_bmp(destination, atlas.width, atlas.height,
                                   atlas.pixels, &error))
        {
            logging::console("lightmap: %s\n", error.c_str());
            return 1;
        }
        logging::console("wrote %s (%ux%u, %d face%s, %d style tile%s)\n",
                         destination.c_str(), atlas.width, atlas.height, atlas.faces,
                         atlas.faces == 1 ? "" : "s", atlas.tiles,
                         atlas.tiles == 1 ? "" : "s");

        if (!manifest_path.empty())
        {
            if (!write_manifest(manifest_path, map, atlas, tiles, &error))
            {
                logging::console("lightmap: %s\n", error.c_str());
                return 1;
            }
            logging::console("wrote %s\n", manifest_path.c_str());
        }
        return 0;
    }
}
