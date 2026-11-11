#include "collision.h"

#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
const char *cube = R"MAP(
{
"classname" "worldspawn"
{
( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) __TB_empty 0 0 0 1 1
( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) __TB_empty 0 0 0 1 1
( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) __TB_empty 0 0 0 1 1
( 0 64 0 ) ( 64 64 0 ) ( 0 64 64 ) __TB_empty 0 0 0 1 1
( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) __TB_empty 0 0 0 1 1
( 0 0 64 ) ( 0 64 64 ) ( 64 0 64 ) __TB_empty 0 0 0 1 1
}
}
)MAP";

void require(bool value)
{
    if (!value)
        throw std::runtime_error("collision regression");
}

int contents(const format::map_data &map, int hull, float x, float y, float z)
{
    int index = map.models.at(0).headnode[hull];
    while (index >= 0)
    {
        const auto &node = map.clipnodes.at(static_cast<size_t>(index));
        const auto &p = map.planes.at(static_cast<size_t>(node.planenum));
        float d = p.normal[0] * x + p.normal[1] * y + p.normal[2] * z - p.dist;
        index = node.children[d >= 0 ? 0 : 1];
    }
    return index;
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        using namespace hltools::collision;
        if (argc == 4)
        {
            std::ifstream input(argv[1], std::ios::binary);
            require(input.good());
            std::string text{std::istreambuf_iterator<char>(input), {}};
            auto output = compile(std::move(text),
                                  static_cast<clip_type>(std::stoi(argv[2])));
            std::ofstream file(argv[3], std::ios::binary);
            auto write = [&](const auto *data, size_t count)
            {
                file.write(reinterpret_cast<const char *>(data),
                           static_cast<std::streamsize>(count * sizeof(*data)));
            };
            uint32_t count = static_cast<uint32_t>(output.bsp.planes.size());
            write(&count, 1);
            write(output.bsp.planes.data(), count);
            count = static_cast<uint32_t>(output.bsp.clipnodes.size());
            write(&count, 1);
            write(output.bsp.clipnodes.data(), count);
            count = static_cast<uint32_t>(output.bsp.models.size());
            write(&count, 1);
            for (const auto &model : output.bsp.models)
                write(model.headnode, 4);
            require(file.good());
            return 0;
        }
        for (auto clip :
             {clip_type::smallest, clip_type::normalized, clip_type::simple,
              clip_type::precise, clip_type::legacy, clip_type::legacytrim})
        {
            auto output = compile(cube, clip);
            require(output.bsp.models.size() == 1);
            require(contents(output.bsp, 1, 32, 32, 99) == -2);
            require(contents(output.bsp, 1, 32, 32, 101) == -1);
            require(contents(output.bsp, 3, 32, 32, 81) == -2);
            require(contents(output.bsp, 3, 32, 32, 83) == -1);
        }
        bool failed = false;
        try
        {
            compile("not a map");
        }
        catch (const std::runtime_error &)
        {
            failed = true;
        }
        require(failed);
        auto a = std::async(std::launch::async, [] { return compile(cube); });
        auto b = std::async(std::launch::async, [] { return compile(cube); });
        require(a.get().bsp.clipnodes.size() == b.get().bsp.clipnodes.size());
        require(
            compile("{\n\"classname\" \"worldspawn\"\n}\n").bsp.models.size() ==
            1);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
