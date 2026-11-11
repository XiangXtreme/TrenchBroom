#pragma once

#include <string>
#include <vector>

#include "direct_clip.h"

namespace direct_clip
{
    bool write_file(const std::string &path, const std::vector<model> &models);
    bool read_file(const std::string &path, std::vector<model> &models);
}
