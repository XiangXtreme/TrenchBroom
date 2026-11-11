#include "direct_clip_io.h"

#include <cstring>

#include "filesystem.h"

namespace direct_clip
{
    namespace
    {
        constexpr unsigned magic = 0x31455850u; // PEX1
        constexpr unsigned version = 1;

        template <typename T>
        void append(std::vector<byte> &out, const T &value)
        {
            const byte *p = reinterpret_cast<const byte *>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }

        template <typename T>
        bool read(const std::vector<byte> &data, size_t &pos, T &value)
        {
            if (pos > data.size() || sizeof(T) > data.size() - pos) return false;
            std::memcpy(&value, data.data() + pos, sizeof(T));
            pos += sizeof(T);
            return true;
        }

        void append_tree(std::vector<byte> &out, const tree &tree)
        {
            unsigned char valid = tree.valid ? 1 : 0;
            append(out, valid);
            unsigned char padding[3] = {};
            out.insert(out.end(), padding, padding + 3);
            unsigned planes = (unsigned)tree.planes.size();
            unsigned nodes = (unsigned)tree.nodes.size();
            append(out, planes); append(out, nodes); append(out, tree.head);
            for (int a = 0; a < 3; a++) append(out, (double)tree.mins[a]);
            for (int a = 0; a < 3; a++) append(out, (double)tree.maxs[a]);
            for (const auto &p : tree.planes)
            {
                for (int a = 0; a < 3; a++) append(out, (double)p.normal[a]);
                append(out, (double)p.dist);
            }
            for (const auto &n : tree.nodes)
            {
                append(out, n.plane); append(out, n.front); append(out, n.back);
            }
        }

        bool read_tree(const std::vector<byte> &data, size_t &pos, tree &tree)
        {
            unsigned char valid = 0, padding[3];
            unsigned planes = 0, nodes = 0;
            if (!read(data, pos, valid) || !read(data, pos, padding)
                || !read(data, pos, planes) || !read(data, pos, nodes)
                || !read(data, pos, tree.head)) return false;
            tree.valid = valid != 0;
            for (int a = 0; a < 3; a++) { double v; if (!read(data, pos, v)) return false; tree.mins[a] = (vec_t)v; }
            for (int a = 0; a < 3; a++) { double v; if (!read(data, pos, v)) return false; tree.maxs[a] = (vec_t)v; }
            if (planes > 1000000 || nodes > 1000000) return false;
            tree.planes.resize(planes); tree.nodes.resize(nodes);
            for (auto &p : tree.planes)
            {
                double v[4]; for (double &x : v) if (!read(data, pos, x)) return false;
                p.normal = {(vec_t)v[0], (vec_t)v[1], (vec_t)v[2]}; p.dist = (vec_t)v[3];
            }
            for (auto &n : tree.nodes)
                if (!read(data, pos, n.plane) || !read(data, pos, n.front) || !read(data, pos, n.back)) return false;
            return true;
        }
    }

    bool write_file(const std::string &path, const std::vector<model> &models)
    {
        std::vector<byte> data;
        append(data, magic); append(data, version);
        unsigned count = (unsigned)models.size(); append(data, count);
        for (const auto &model : models)
        {
            unsigned char valid = model.valid ? 1 : 0;
            append(data, valid);
            unsigned char padding[3] = {}; data.insert(data.end(), padding, padding + 3);
            for (const auto &tree : model.hulls) append_tree(data, tree);
        }
        return fs::write_all(path, data.data(), data.size());
    }

    bool read_file(const std::string &path, std::vector<model> &models)
    {
        std::vector<byte> data;
        if (!fs::read_all(path, data)) return false;
        size_t pos = 0; unsigned got_magic = 0, got_version = 0, count = 0;
        if (!read(data, pos, got_magic) || !read(data, pos, got_version)
            || !read(data, pos, count) || got_magic != magic || got_version != version
            || count > 100000) return false;
        models.resize(count);
        for (auto &model : models)
        {
            unsigned char valid = 0, padding[3];
            if (!read(data, pos, valid) || !read(data, pos, padding)) return false;
            model.valid = valid != 0;
            for (auto &tree : model.hulls) if (!read_tree(data, pos, tree)) return false;
        }
        return pos == data.size();
    }
}
