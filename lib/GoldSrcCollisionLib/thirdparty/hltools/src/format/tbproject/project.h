#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/types.h"

// Read only access to a TrenchBroom ".tbproj" catalog.
//
// A project is a SQLite database. The compiler only needs what a map references,
// so this exposes the map source and the compiled texture wads and nothing else.
// Schema versions 1 through 4 are accepted; the database is always opened read only
// so an editor that is still running keeps ownership of the file.
namespace format::tbproject
{
    struct asset
    {
        std::string id;
        std::uint64_t revision = 0;
        std::string type;
        std::string virtual_path;
        std::string publish_path;
        std::string display_name;
        std::vector<byte> compatible;
    };

    struct terrain_compile
    {
        std::vector<byte> map;
        std::vector<byte> wad;
    };

    class project
    {
    public:
        static std::unique_ptr<project> open(const std::string &path,
                                            std::string *error);

        ~project();
        project(const project &) = delete;
        project &operator=(const project &) = delete;

        const std::string &name() const {
            return name_;
        }
        const std::string &game() const {
            return game_;
        }
        const std::vector<asset> &assets() const {
            return assets_;
        }

        // Finds a map by display name or by the stem of its virtual or published
        // path, so "alpha", "alpha.map" and "maps/alpha.map" all match.
        const asset *find_map(const std::string &name) const;

        // Finds a compiled texture by its engine facing name.
        const asset *find_texture(const std::string &name) const;

        // Only use terrain output generated for this exact saved map revision.
        bool read_terrain_compile(const asset &map, terrain_compile &out,
                                  std::string *error) const;

    private:
        struct database;
        project() = default;

        std::string name_;
        std::string game_;
        int schema_ = 0;
        std::vector<asset> assets_;
        std::unique_ptr<database> database_;
    };
}
