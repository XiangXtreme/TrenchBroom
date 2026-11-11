#pragma once

#include <string>
#include <vector>

namespace tools
{
    // true when the command line asks for a project driven compile
    bool project_compile_requested(int argc, char **argv);

    struct project_input
    {
        // rewritten command line, including argv[0]
        std::vector<std::string> arguments;
        // materialized input directory (map source, texture wads, wad list)
        std::string working_directory;
        // bsp the compile writes inside working_directory
        std::string bsp_path;
        // final bsp destination
        std::string output_path;
    };

    // Expands "--project <file> --map <name>" into a normal compile invocation:
    // the map source and its compiled texture wads are written to a temporary
    // directory and the command line is rewritten to compile that map with the
    // generated wad list.
    bool expand_project_compile(int argc, char **argv, project_input &input,
                                std::string *error);

    // Removes the materialized inputs.
    void discard_project_input(project_input &input);
}
