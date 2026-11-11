#include "project.h"

#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace format::tbproject
{
    namespace
    {
        constexpr int project_application_id = 0x5442504a;
        constexpr int oldest_schema = 1;
        constexpr int newest_schema = 4;

        std::string lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) {
                               return (char)std::tolower(c);
                           });
            return text;
        }

        std::string stem_of(const std::string &path)
        {
            return std::filesystem::path(path).stem().string();
        }

        std::string column_text(sqlite3_stmt *statement, int column)
        {
            const unsigned char *text = sqlite3_column_text(statement, column);
            return text ? std::string(reinterpret_cast<const char *>(text)) : std::string();
        }

        std::vector<byte> column_blob(sqlite3_stmt *statement, int column)
        {
            const void *data = sqlite3_column_blob(statement, column);
            const int size = sqlite3_column_bytes(statement, column);
            if (!data || size <= 0)
                return {};
            const byte *begin = static_cast<const byte *>(data);
            return std::vector<byte>(begin, begin + size);
        }

        bool query_int(sqlite3 *database, const char *sql, int &out)
        {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            const bool ok = sqlite3_step(statement) == SQLITE_ROW;
            if (ok)
                out = sqlite3_column_int(statement, 0);
            sqlite3_finalize(statement);
            return ok;
        }

        void set_error(sqlite3 *database, std::string *error, const std::string &message)
        {
            if (!error)
                return;
            *error = message;
            if (database)
                *error += std::string(": ") + sqlite3_errmsg(database);
        }
    }

    struct project::database
    {
        sqlite3 *handle = nullptr;
        ~database()
        {
            if (handle)
                sqlite3_close(handle);
        }
    };

    project::~project() = default;

    std::unique_ptr<project> project::open(const std::string &path, std::string *error)
    {
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(path, file_error))
        {
            if (error)
                *error = "project file does not exist: " + path;
            return nullptr;
        }

        auto handle = std::make_unique<database>();
        const int rc = sqlite3_open_v2(path.c_str(), &handle->handle,
                                       SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK)
        {
            const std::string message = handle->handle
                ? std::string("could not open project file: ")
                    + sqlite3_errmsg(handle->handle)
                : std::string("could not open project file");
            if (error)
                *error = message;
            return nullptr;
        }
        sqlite3_busy_timeout(handle->handle, 2000);
        // Keep the asset heads and terrain cache on the same SQLite snapshot.
        if (sqlite3_exec(handle->handle, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            set_error(handle->handle, error, "could not read project snapshot");
            return nullptr;
        }

        int application_id = 0;
        if (!query_int(handle->handle, "PRAGMA application_id", application_id)
            || application_id != project_application_id)
        {
            if (error)
                *error = "not a TrenchBroom project file: " + path;
            return nullptr;
        }

        int schema = 0;
        if (!query_int(handle->handle, "PRAGMA user_version", schema)
            || schema < oldest_schema)
        {
            if (error)
                *error = "unsupported project schema version "
                    + std::to_string(schema) + " (minimum supported: "
                    + std::to_string(oldest_schema) + ")";
            return nullptr;
        }

        auto result = std::unique_ptr<project>(new project());
        result->database_ = std::move(handle);
        result->schema_ = schema;

        // schema 3 learned a separate publish path; older projects published under
        // their virtual path
        const std::string publish_column =
            schema >= 3 ? "r.publish_path" : "r.virtual_path";
        const std::string asset_query =
            "SELECT r.type, r.virtual_path, " + publish_column
            + ", r.display_name, c.data, r.asset_id, r.revision FROM heads h "
              "JOIN revisions r ON r.asset_id = h.asset_id AND r.revision = h.revision "
              "JOIN content c ON c.id = r.compatible";

        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(result->database_->handle, asset_query.c_str(), -1,
                               &statement, nullptr) != SQLITE_OK)
        {
            set_error(result->database_->handle, error, "could not read project assets");
            return nullptr;
        }
        int step = SQLITE_OK;
        while ((step = sqlite3_step(statement)) == SQLITE_ROW)
        {
            asset entry;
            entry.type = column_text(statement, 0);
            entry.virtual_path = column_text(statement, 1);
            entry.publish_path = column_text(statement, 2);
            entry.display_name = column_text(statement, 3);
            entry.compatible = column_blob(statement, 4);
            entry.id = column_text(statement, 5);
            const sqlite3_int64 revision = sqlite3_column_int64(statement, 6);
            if (entry.id.empty() || revision <= 0)
            {
                sqlite3_finalize(statement);
                if (error)
                    *error = "project contains an invalid asset revision";
                return nullptr;
            }
            entry.revision = static_cast<std::uint64_t>(revision);
            result->assets_.push_back(std::move(entry));
        }
        if (step != SQLITE_DONE)
        {
            set_error(result->database_->handle, error, "could not read project assets");
            sqlite3_finalize(statement);
            return nullptr;
        }
        sqlite3_finalize(statement);

        sqlite3_stmt *info = nullptr;
        if (sqlite3_prepare_v2(result->database_->handle,
                               "SELECT name, game_id FROM project", -1, &info,
                               nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(info) == SQLITE_ROW)
            {
                result->name_ = column_text(info, 0);
                result->game_ = column_text(info, 1);
            }
            sqlite3_finalize(info);
        }
        return result;
    }

    const asset *project::find_map(const std::string &name) const
    {
        const std::string wanted = lower(name);
        const std::string wanted_stem = lower(stem_of(name));
        for (const asset &entry : assets_)
        {
            if (lower(entry.type) != "map")
                continue;
            if (lower(entry.display_name) == wanted
                || lower(stem_of(entry.virtual_path)) == wanted
                || lower(stem_of(entry.publish_path)) == wanted
                || lower(stem_of(entry.virtual_path)) == wanted_stem)
                return &entry;
        }
        return nullptr;
    }

    const asset *project::find_texture(const std::string &name) const
    {
        const std::string wanted = lower(name);
        for (const asset &entry : assets_)
        {
            if (lower(entry.type) != "texture")
                continue;
            if (lower(entry.display_name) == wanted)
                return &entry;
        }
        return nullptr;
    }

    bool project::read_terrain_compile(const asset &map, terrain_compile &out,
                                       std::string *error) const
    {
        const std::string instructions =
            "open and save this project with an up-to-date TrenchBroom";

        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(database_->handle,
                               "SELECT source_revision,generator_version,map,wad,error "
                               "FROM terrain_compile WHERE asset_id=?", -1,
                               &statement, nullptr) != SQLITE_OK)
        {
            if (error)
                *error = "terrain compile data is missing; " + instructions;
            return false;
        }
        if (sqlite3_bind_text(statement, 1, map.id.c_str(), -1, SQLITE_TRANSIENT)
            != SQLITE_OK)
        {
            set_error(database_->handle, error, "could not read terrain compile data");
            sqlite3_finalize(statement);
            return false;
        }
        const int rc = sqlite3_step(statement);
        if (rc == SQLITE_DONE)
        {
            sqlite3_finalize(statement);
            if (error)
                *error = "terrain compile data is missing; " + instructions;
            return false;
        }
        if (rc != SQLITE_ROW)
        {
            set_error(database_->handle, error, "could not read terrain compile data");
            sqlite3_finalize(statement);
            return false;
        }
        const sqlite3_int64 revision = sqlite3_column_int64(statement, 0);
        const sqlite3_int64 generator = sqlite3_column_int64(statement, 1);
        auto compiled_map = column_blob(statement, 2);
        auto compiled_wad = column_blob(statement, 3);
        const std::string problem = column_text(statement, 4);
        sqlite3_finalize(statement);

        if (revision <= 0 || static_cast<std::uint64_t>(revision) != map.revision
            || generator <= 0)
        {
            if (error)
                *error = "terrain compile data is outdated; " + instructions;
            return false;
        }
        if (!problem.empty())
        {
            if (error)
                *error = "terrain could not be prepared: " + problem;
            return false;
        }
        if (compiled_map.empty() || compiled_wad.empty())
        {
            if (error)
                *error = "terrain compile data is incomplete; " + instructions;
            return false;
        }
        out.map = std::move(compiled_map);
        out.wad = std::move(compiled_wad);
        return true;
    }
}
