#include "../cli/hltools/compile.h"
#include "../cli/tools/lightmap_tool.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// One compiler invocation per process. The editor owns the scratch directory,
// progress pipe and cancellation; fatal stage errors exit only this worker.
namespace
{
int run_worker(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "compile") == 0)
        return tools::run_compile(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "lightmap") == 0)
        return tools::run_lightmap_tool(argc - 1, argv + 1);
    std::fprintf(
        stderr,
        "usage: HltoolsLightmapWorker <compile|lightmap> [arguments]\n");
    return 1;
}

#ifdef _WIN32
bool to_utf8(const wchar_t *argument, std::string &result)
{
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
                            nullptr, 0, nullptr, nullptr);
    if (size == 0)
        return false;

    result.resize(static_cast<std::size_t>(size));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
                            result.data(), size, nullptr, nullptr) != size)
        return false;
    result.resize(static_cast<std::size_t>(size - 1));
    return true;
}
#endif
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
    {
        std::string argument;
        if (!to_utf8(wide_argv[i], argument))
        {
            std::fprintf(
                stderr,
                "error: could not convert a command-line argument to UTF-8\n");
            return 1;
        }
        arguments.push_back(std::move(argument));
    }

    std::vector<char *> utf8_argv;
    utf8_argv.reserve(arguments.size());
    for (auto &argument : arguments)
        utf8_argv.push_back(argument.data());
    return run_worker(argc, utf8_argv.data());
}
#else
int main(int argc, char **argv) { return run_worker(argc, argv); }
#endif
