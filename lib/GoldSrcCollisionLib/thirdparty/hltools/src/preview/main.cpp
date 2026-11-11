#include "../cli/hltools/compile.h"
#include "../cli/tools/lightmap_tool.h"

#include <cstdio>
#include <cstring>

// One compiler invocation per process. The editor owns the scratch directory,
// progress pipe and cancellation; fatal stage errors exit only this worker.
int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "compile") == 0)
        return tools::run_compile(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "lightmap") == 0)
        return tools::run_lightmap_tool(argc - 1, argv + 1);
    std::fprintf(stderr, "usage: HltoolsLightmapWorker <compile|lightmap> [arguments]\n");
    return 1;
}
