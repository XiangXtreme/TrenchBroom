#include "preview_cache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "common/cmdline.h"
#include "common/filesystem.h"
#include "common/log.h"
#include "format/bsp/file.h"
#include "format/map/document.h"

namespace tools
{
    namespace
    {
        bool light_class(const char *name)
        {
            return std::strcmp(name, "light") == 0
                || std::strcmp(name, "light_spot") == 0
                || std::strcmp(name, "light_environment") == 0;
        }

        bool light_property(const std::string &key)
        {
            // Everything else, including classname, style, targetname, spawnflags
            // and compiler-specific properties, stays in the exact cache key.
            return key == "_light" || key == "origin" || key == "angles"
                || key == "angle" || key == "pitch" || key == "_cone"
                || key == "_cone2" || key == "_fade" || key == "_falloff";
        }

        bool light_positions_valid(const format::map_data &map,
                                   const csg::map_source &source)
        {
            if (map.models.empty()) return false;
            for (const auto &entity : source.entities)
            {
                if (!light_class(entity.value("classname"))) continue;
                float point[3];
                if (std::sscanf(entity.value("origin"), "%f %f %f",
                                &point[0], &point[1], &point[2]) != 3
                    || !std::isfinite(point[0]) || !std::isfinite(point[1])
                    || !std::isfinite(point[2])) return false;
                // BSP uses origin + 1 for outside filling and both +/- 1 for
                // inside filling. All probes must stay in existing empty space.
                for (float offset : {-1.0f, 0.0f, 1.0f})
                {
                    int node = map.models[0].headnode[0];
                    std::size_t budget = map.nodes.size() + 1;
                    while (node >= 0 && budget--)
                    {
                        if ((std::size_t)node >= map.nodes.size()) return false;
                        const auto &n = map.nodes[(std::size_t)node];
                        if ((std::size_t)n.planenum >= map.planes.size()) return false;
                        const auto &p = map.planes[(std::size_t)n.planenum];
                        const float d = (point[0] * p.normal[0] + point[1] * p.normal[1])
                            + (point[2] + offset) * p.normal[2] - p.dist;
                        node = n.children[d >= 0 ? 0 : 1];
                    }
                    const int leaf = -node - 1;
                    if (node >= 0 || leaf < 0 || (std::size_t)leaf >= map.leafs.size()
                        || map.leafs[(std::size_t)leaf].contents != -1) return false;
                }
            }
            return true;
        }
    }

    preview_cache::preview_cache(const std::string &path, const std::string &source_text,
                                const csg::map_source &source,
                                const csg::csg_options &options, int argc, char **argv)
    {
        if (path.empty() || !options.hull_file_path.empty()
            || !options.invisible_items.empty()) return;
        for (const auto &entity : source.entities)
            if (entity.int_value("zhlt_studiobsp") != 0) return;

        identity_ = "hltools-preview-v1\n";
        std::size_t cursor = 0;
        for (const auto &entity : format::parse_map_source_entities(source_text))
        {
            identity_.append(source_text, cursor, entity.begin - cursor);
            const std::string raw = source_text.substr(entity.begin, entity.end - entity.begin);
            // A light with brushes is not a point light and must stay exact.
            if (light_class(entity.keyvalues.value("classname"))
                && raw.find('{', 1) == std::string::npos)
            {
                format::entity filtered;
                for (const auto &pair : entity.keyvalues.pairs())
                    if (!light_property(pair.first)) filtered.append(pair.first, pair.second);
                identity_ += format::write_entities({filtered});
            }
            else identity_ += raw;
            cursor = entity.end;
        }
        identity_.append(source_text, cursor, std::string::npos);
        for (int i = 1; i < argc; ++i)
        {
            const std::string flag = argv[i];
            if (flag.empty() || flag[0] != '-') continue;
            const int arity = cli::flag_arity(flag);
            if (flag != "-previewcache" && flag != "-threads" && flag != "-wadcfgfile")
            {
                identity_ += "\n" + flag;
                for (int j = 1; j <= arity && i + j < argc; ++j)
                    identity_ += "\n" + std::string(argv[i + j]);
            }
            i += arity;
        }
        if (!csg::preview_wad_identity(source, options.wad, identity_))
        {
            identity_.clear();
            return;
        }
        path_ = path;
    }

    bool preview_cache::restore(format::map_data &map, const csg::map_source &source,
                                const std::string &base) const
    {
        if (path_.empty()) return false;
        std::vector<byte> stored;
        if (!fs::read_all(path_ + ".key", stored)
            || std::string(stored.begin(), stored.end()) != identity_
            || !format::bsp_file::load(path_ + ".bsp", map)
            || !light_positions_valid(map, source)) return false;
        if (!fs::read_all(path_ + ".wa_", stored)
            || !fs::write_all(base + ".wa_", stored.data(), stored.size())) return false;
        logging::console("preview: reusing geometry and visibility; relighting entities\n");
        return true;
    }

    void preview_cache::save(const format::map_data &map, const std::string &base) const
    {
        if (path_.empty()) return;
        std::vector<byte> wad;
        // Write the identity last, so interrupted saves never publish a new key.
        std::remove((path_ + ".key").c_str());
        if (!fs::read_all(base + ".wa_", wad)
            || !format::bsp_file::write(path_ + ".bsp", map)
            || !fs::write_all(path_ + ".wa_", wad.data(), wad.size())
            || !fs::write_all(path_ + ".key", identity_.data(), identity_.size()))
            logging::warn("could not save the preview geometry cache");
    }
}
