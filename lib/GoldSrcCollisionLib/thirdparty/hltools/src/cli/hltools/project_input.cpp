#include "project_input.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "common/filesystem.h"
#include "format/bmp/image.h"
#include "format/miptex/texture.h"
#include "format/tbproject/project.h"
#include "format/wad/archive.h"

namespace tools
{
    namespace
    {
        std::string lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });
            return text;
        }

        bool is_flag(const std::string &token)
        {
            return !token.empty() && token[0] == '-';
        }

        // returns the value of a "--name"/"-name" option, or nullptr
        const char *option_value(int argc, char **argv, const char *long_name,
                                 const char *short_name, int &index)
        {
            for (int i = 1; i + 1 < argc; i++)
            {
                const std::string token = argv[i];
                if (token == long_name || token == short_name)
                {
                    index = i;
                    return argv[i + 1];
                }
            }
            return nullptr;
        }

        std::string safe_file_stem(const std::string &name, int fallback)
        {
            std::string stem;
            for (const char c : name)
            {
                const bool allowed = (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '_' || c == '-';
                stem += allowed ? c : '_';
            }
            if (stem.empty())
                stem = "asset" + std::to_string(fallback);
            return stem;
        }

        bool has_magic(const std::vector<byte> &data, const char *magic)
        {
            return data.size() >= 4
                && data[0] == (byte)magic[0] && data[1] == (byte)magic[1]
                && data[2] == (byte)magic[2] && data[3] == (byte)magic[3];
        }

        bool has_terrain_paint(const std::vector<byte> &data)
        {
            constexpr char marker[] = "\"_tb_terrain_paint\"";
            return std::search(data.begin(), data.end(), marker,
                               marker + sizeof(marker) - 1) != data.end();
        }

        bool has_empty_texture(const std::vector<byte> &data)
        {
            constexpr char marker[] = "__tb_empty";
            return std::search(data.begin(), data.end(), marker,
                               marker + sizeof(marker) - 1,
                               [](byte actual, char expected) {
                                   return std::tolower(actual) == expected;
                               }) != data.end();
        }

        std::string unique_directory()
        {
            const auto now = std::chrono::high_resolution_clock::now()
                                 .time_since_epoch()
                                 .count();
            static int counter = 0;
            return (
#ifdef HLTOOLS_PREVIEW_ONLY
                    std::filesystem::current_path()
#else
                    std::filesystem::temp_directory_path()
#endif
                    / ("hltools-tbproj-" + std::to_string(now) + "-"
                       + std::to_string(counter++)))
                .string();
        }

        bool append_wad_path(std::string &list, const std::string &path,
                             std::string *error, bool include = false)
        {
            // The WAD config parser accepts quoted paths but does not support
            // escapes inside quotes.
            if (path.find_first_of("\"\r\n") != std::string::npos)
            {
                if (error)
                    *error = "WAD path cannot be represented in the compiler configuration";
                return false;
            }
            if (include)
                list += "include ";
            list += '"' + path + "\"\n";
            return true;
        }

        bool write_empty_texture_wad(const std::string &path, std::string *error)
        {
            // Match TrenchBroom's built-in __TB_empty checkerboard without
            // requiring the project to capture the editor's internal asset.
            format::indexed_image image;
            image.width = image.height = 32;
            image.palette[0] = {191, 95, 96};
            image.palette[1] = {129, 64, 65};
            image.pixels.resize(32 * 32);
            for (unsigned y = 0; y < image.height; y++)
                for (unsigned x = 0; x < image.width; x++)
                    image.pixels[y * image.width + x] = (byte)((x / 16 + y / 16) % 2);

            format::mip_texture texture;
            std::string detail;
            if (!format::build_mip_texture("__TB_empty", image, texture, &detail)
                || !format::write_wad3(path, {texture}, &detail))
            {
                if (error)
                    *error = "could not prepare the built-in placeholder texture: " + detail;
                return false;
            }
            return true;
        }

    }

    bool project_compile_requested(int argc, char **argv)
    {
        int index = 0;
        return option_value(argc, argv, "--project", "-project", index) != nullptr;
    }

    bool expand_project_compile(int argc, char **argv, project_input &input,
                                std::string *error)
    {
        int project_index = 0;
        int map_index = 0;
        int output_index = 0;
        const char *project_path =
            option_value(argc, argv, "--project", "-project", project_index);
        const char *map_name =
            option_value(argc, argv, "--map", "-map", map_index);
        const char *output =
            option_value(argc, argv, "--output", "-output", output_index);

        if (!project_path)
            return false;
        if (!map_name)
        {
            if (error)
                *error = "--project requires --map <name>";
            return false;
        }
        int wad_config_index = 0;
        if (option_value(argc, argv, "-wadcfgfile", "-wadcfgfile", wad_config_index))
        {
            if (error)
                *error = "--project builds its own wad list; do not pass -wadcfgfile";
            return false;
        }

        auto project = format::tbproject::project::open(project_path, error);
        if (!project)
            return false;

        const format::tbproject::asset *map = project->find_map(map_name);
        if (!map)
        {
            std::string names;
            for (const auto &entry : project->assets())
            {
                if (lower(entry.type) != "map")
                    continue;
                if (!names.empty())
                    names += ", ";
                names += entry.display_name;
            }
            if (error)
                *error = std::string("project has no map named '") + map_name
                    + "' (maps: " + (names.empty() ? "none" : names) + ")";
            return false;
        }

        format::tbproject::terrain_compile terrain;
        const bool painted_terrain = has_terrain_paint(map->compatible);
        if (painted_terrain && !project->read_terrain_compile(*map, terrain, error))
            return false;
        if (painted_terrain
            && (!has_magic(terrain.wad, "WAD3") || has_terrain_paint(terrain.map)))
        {
            if (error)
                *error = "invalid terrain compile data; open and save this project "
                         "with an up-to-date TrenchBroom";
            return false;
        }

        const std::string map_stem =
            std::filesystem::path(map->virtual_path).stem().string();
        const std::string map_file = map_stem.empty() ? std::string("map") : map_stem;

        input.working_directory = unique_directory();
        if (!fs::make_directory(input.working_directory + "/textures"))
        {
            if (error)
                *error = "could not create temporary directory "
                    + input.working_directory;
            return false;
        }

        const std::string map_path =
            (std::filesystem::path(input.working_directory)
             / (map_file + ".map"))
                .string();
        const auto &map_source = painted_terrain ? terrain.map : map->compatible;
        if (map_source.empty()
            || !fs::write_all(map_path, map_source.data(), map_source.size()))
        {
            if (error)
                *error = "could not write the map source extracted from the project";
            return false;
        }

        std::string wad_list;
        format::wad_archive generated_wad;
        bool missing_empty = has_empty_texture(map_source);
        if (painted_terrain)
        {
            const std::string wad_path =
                (std::filesystem::path(input.working_directory) / "terrain-generated.wad")
                    .string();
            if (!fs::write_all(wad_path, terrain.wad.data(), terrain.wad.size())
                || !generated_wad.load(wad_path, error))
            {
                if (error && error->empty())
                    *error = "could not extract terrain textures from the project";
                return false;
            }
            if (!append_wad_path(wad_list, wad_path, error))
                return false;
            if (generated_wad.find_texture("__TB_empty"))
                missing_empty = false;
        }
        int texture_index = 0;
        for (const auto &entry : project->assets())
        {
            if (lower(entry.type) != "texture" || entry.compatible.empty())
                continue;
            texture_index++;
            if (!has_magic(entry.compatible, "WAD3"))
            {
                if (error)
                    *error = "texture '" + entry.display_name
                        + "' is not a compiled wad; re-import or convert it in "
                          "TrenchBroom";
                return false;
            }
            const std::string wad_path =
                (std::filesystem::path(input.working_directory) / "textures"
                 / (safe_file_stem(entry.display_name, texture_index) + ".wad"))
                    .string();
            if (!fs::write_all(wad_path, entry.compatible.data(),
                               entry.compatible.size()))
            {
                if (error)
                    *error = "could not write texture wad " + wad_path;
                return false;
            }
            if (painted_terrain || missing_empty)
            {
                format::wad_archive asset_wad;
                std::string wad_error;
                if (!asset_wad.load(wad_path, &wad_error))
                {
                    if (error)
                        *error = "could not read texture '" + entry.display_name
                            + "': " + wad_error;
                    return false;
                }
                if (asset_wad.find_texture("__TB_empty"))
                    missing_empty = false;
                for (const auto &lump : generated_wad.lumps())
                {
                    if (asset_wad.find_texture(lump.name))
                    {
                        if (error)
                            *error = "generated terrain texture conflicts with project "
                                     "texture: "
                                + std::string(lump.name,
                                              std::find(lump.name,
                                                        lump.name + format::max_wad_name,
                                                        '\0'));
                        return false;
                    }
                }
            }
            if (!append_wad_path(wad_list, wad_path, error))
                return false;
        }

        // The editor's placeholder is an internal asset rather than a project
        // texture. A project compile must provide it independently of whether a
        // lightmap preview or MAP export happened beforehand.
        if (missing_empty)
        {
            const std::string fallback_path =
                (std::filesystem::path(input.working_directory) / "tb-empty.wad").string();
            // The scratch WAD is deleted after compilation, so embed its texture
            // even when the caller requests external WAD textures.
            if (!write_empty_texture_wad(fallback_path, error)
                || !append_wad_path(wad_list, fallback_path, error, true))
                return false;
        }

        const std::string wad_config =
            (std::filesystem::path(input.working_directory) / "wads.cfg").string();
        if (!fs::write_all(wad_config, wad_list.data(), wad_list.size()))
        {
            if (error)
                *error = "could not write the wad list for the project";
            return false;
        }

        // keep everything the caller passed except the project options, then add
        // the wad list and the extracted map last so the map is the final
        // positional argument
        input.arguments.clear();
        input.arguments.push_back(argv[0]);
        for (int i = 1; i < argc; i++)
        {
            if (i == project_index || i == project_index + 1 || i == map_index
                || i == map_index + 1
                || (output && (i == output_index || i == output_index + 1)))
                continue;
            input.arguments.push_back(argv[i]);
        }
        input.arguments.push_back("-wadcfgfile");
        input.arguments.push_back(wad_config);
        input.arguments.push_back(map_path);

        input.bsp_path = (std::filesystem::path(input.working_directory)
                          / (map_file + ".bsp"))
                             .string();
        if (output && *output)
        {
            input.output_path = output;
        }
        else
        {
            input.output_path =
                (std::filesystem::path(project_path).parent_path()
                 / (map_file + ".bsp"))
                    .string();
        }
        return true;
    }

    void discard_project_input(project_input &input)
    {
        if (input.working_directory.empty())
            return;
        std::error_code ignored;
        std::filesystem::remove_all(input.working_directory, ignored);
        input.working_directory.clear();
    }
}
