#pragma once

#include <string>
#include "csg/csg.h"
#include "format/bsp/data.h"

namespace tools
{
    // A private cache directory owned by the editor for one document session.
    // Only pre-RAD data is saved: RAD mutates faces, textures and texinfos.
    class preview_cache
    {
    public:
        preview_cache(const std::string &path, const std::string &source_text,
                      const csg::map_source &source, const csg::csg_options &options,
                      int argc, char **argv);
        bool restore(format::map_data &map, const csg::map_source &source,
                     const std::string &base) const;
        void save(const format::map_data &map, const std::string &base) const;
    private:
        std::string path_;
        std::string identity_;
    };
}
