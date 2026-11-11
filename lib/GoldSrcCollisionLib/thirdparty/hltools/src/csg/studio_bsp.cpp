#include "studio_bsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../common/filesystem.h"
#include "../common/log.h"
#include "../common/string_util.h"
#include "format/mdl/goldsrc/read.h"

// zhlt_studiobsp: bake a studio model into the map as solid geometry.
//
// every source triangle becomes a watertight five sided prism (front, back and
// three sides) in world space, which is the reference's fallback when a closed
// shell cannot be built. the prisms are ordinary detail brushes, so csg, bsp,
// vis and rad treat them like func_detail geometry and RAD can still shadow
// them through the resulting faces.

namespace csg
{
    namespace
    {
        constexpr double mdl_pi = 3.14159265358979323846;
        constexpr float default_thickness = 1.0f;
        constexpr float min_thickness = 0.01f;
        constexpr float max_thickness = 512.0f;
        // prisms thinner than this never survive the csg plane epsilon
        constexpr double minimum_bake_altitude = 0.5;
        constexpr int texture_alignment = 16;
        constexpr int texture_max_size = 512;
        constexpr unsigned opaque_light_flag = 2;

        struct placement
        {
            double m[3][4];
        };

        // the engine's AngleMatrix with the per axis xform scale. angles are in
        // degrees and the caller already applied the negative pitch quirk.
        placement placement_for(const math::vec3v &origin, const math::vec3v &angles,
                                const math::vec3v &scale)
        {
            const double to_radians = mdl_pi * 2.0 / 360.0;
            const double sy = std::sin(angles[1] * to_radians);
            const double cy = std::cos(angles[1] * to_radians);
            const double sp = std::sin(angles[0] * to_radians);
            const double cp = std::cos(angles[0] * to_radians);
            const double sr = std::sin(angles[2] * to_radians);
            const double cr = std::cos(angles[2] * to_radians);

            placement out{};
            out.m[0][0] = cp * cy * scale[0];
            out.m[1][0] = cp * sy * scale[0];
            out.m[2][0] = -sp * scale[0];
            out.m[0][1] = (sr * sp * cy + cr * -sy) * scale[1];
            out.m[1][1] = (sr * sp * sy + cr * cy) * scale[1];
            out.m[2][1] = sr * cp * scale[1];
            out.m[0][2] = (cr * sp * cy + -sr * -sy) * scale[2];
            out.m[1][2] = (cr * sp * sy + -sr * cy) * scale[2];
            out.m[2][2] = cr * cp * scale[2];
            out.m[0][3] = origin[0];
            out.m[1][3] = origin[1];
            out.m[2][3] = origin[2];
            return out;
        }

        math::vec3v place(const placement &p, const float in[3])
        {
            return math::vec3v{
                p.m[0][0] * in[0] + p.m[0][1] * in[1] + p.m[0][2] * in[2] + p.m[0][3],
                p.m[1][0] * in[0] + p.m[1][1] * in[1] + p.m[1][2] * in[2] + p.m[1][3],
                p.m[2][0] * in[0] + p.m[2][1] * in[1] + p.m[2][2] * in[2] + p.m[2][3]};
        }

        std::string uppercase(std::string value)
        {
            for (char &c : value)
                c = (char)std::toupper((unsigned char)c);
            return value;
        }

        std::string texture_stem(const std::string &name)
        {
            std::string stem = name;
            const std::size_t slash = stem.find_last_of("/\\");
            if (slash != std::string::npos)
                stem = stem.substr(slash + 1);
            const std::size_t dot = stem.find_last_of('.');
            if (dot != std::string::npos && dot != 0)
                stem = stem.substr(0, dot);
            return uppercase(stem);
        }

        unsigned texture_hash(const format::studio_texture &texture, unsigned salt)
        {
            unsigned hash = 2166136261u ^ salt;
            const int fields[3] = {(int)texture.image.width, (int)texture.image.height,
                                   texture.flags};
            const byte *field_bytes = (const byte *)fields;
            for (std::size_t i = 0; i < sizeof(fields); i++)
                hash = (hash ^ field_bytes[i]) * 16777619u;
            for (byte value : texture.image.pixels)
                hash = (hash ^ value) * 16777619u;
            for (int i = 0; i < 256; i++)
                for (int c = 0; c < 3; c++)
                    hash = (hash ^ texture.image.palette[i][c]) * 16777619u;
            return hash;
        }

        bool texture_is_masked(const format::studio_texture &texture)
        {
            return (texture.flags & (int)format::studio_nf_masked) != 0
                || (!texture.name.empty() && texture.name[0] == '{');
        }

        // an engine texture name has at most 15 characters. when the source
        // name is too long or collides, a hash suffix keeps it unique while
        // the mask prefix stays in front where the engine expects it.
        std::string hashed_texture_name(const format::studio_texture &texture, unsigned salt)
        {
            std::string base = texture_stem(texture.name);
            if (base.empty())
                base = "MDL_TEXTURE";
            if (texture_is_masked(texture) && base[0] != '{')
                base.insert(base.begin(), '{');

            const std::size_t prefix = (base.size() > 1 && (base[0] == '+' || base[0] == '-'))
                ? 2
                : (base[0] == '{' ? 1 : 0);
            const std::size_t stem_length = 15 - prefix - 9;
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "_%08X", texture_hash(texture, salt));

            std::string name = base.substr(0, prefix);
            name += base.substr(prefix, std::min(stem_length, base.size() - prefix));
            name += suffix;
            return name;
        }

        int bsp_texture_dimension(int dimension)
        {
            if (dimension < 1)
                return 0;
            int target = (dimension + texture_alignment - 1) & ~(texture_alignment - 1);
            return std::min(target, texture_max_size);
        }

        // four bsp mip levels need base dimensions divisible by 16, so a skin
        // with any other size is resampled to the next legal size
        format::indexed_image resize_image(const format::indexed_image &source, int width,
                                           int height)
        {
            format::indexed_image out;
            out.width = (unsigned)width;
            out.height = (unsigned)height;
            out.pixels.resize((std::size_t)width * (std::size_t)height);
            for (int y = 0; y < height; y++)
            {
                const int source_y = (int)(((std::uint64_t)(2 * y + 1) * source.height)
                                           / ((std::uint64_t)2 * height));
                for (int x = 0; x < width; x++)
                {
                    const int source_x = (int)(((std::uint64_t)(2 * x + 1) * source.width)
                                               / ((std::uint64_t)2 * width));
                    out.pixels[(std::size_t)y * width + x] =
                        source.pixels[(std::size_t)source_y * source.width + source_x];
                }
            }
            out.palette = source.palette;
            return out;
        }

        struct texture_registry
        {
            std::map<std::string, format::mip_texture> by_name;
            std::set<std::string> map_names;
            std::vector<format::mip_texture> order;
        };

        bool register_texture(texture_registry &registry,
                              const format::studio_texture &texture, std::string &name)
        {
            const int width = bsp_texture_dimension((int)texture.image.width);
            const int height = bsp_texture_dimension((int)texture.image.height);
            if (!width || !height || texture.image.pixels.empty()
                || texture.image.pixels.size()
                    != (std::size_t)texture.image.width * texture.image.height)
            {
                logging::warn("zhlt_studiobsp: texture '%s' has an unusable image",
                              texture.name.c_str());
                return false;
            }
            const format::indexed_image image = resize_image(texture.image, width, height);

            for (unsigned salt = 0; salt < 1024; salt++)
            {
                const std::string candidate = hashed_texture_name(texture, salt);
                if (registry.map_names.count(candidate))
                    continue;

                format::mip_texture mip;
                std::string error;
                if (!format::build_mip_texture(candidate, image, mip, &error))
                {
                    logging::warn("zhlt_studiobsp: texture '%s' cannot be embedded: %s",
                                  texture.name.c_str(), error.c_str());
                    return false;
                }

                const auto existing = registry.by_name.find(candidate);
                if (existing != registry.by_name.end())
                {
                    if (existing->second.data == mip.data)
                    {
                        name = candidate;
                        return true;
                    }
                    continue;
                }

                name = candidate;
                registry.order.push_back(mip);
                registry.by_name.emplace(candidate, std::move(mip));
                return true;
            }

            logging::warn("zhlt_studiobsp: texture '%s' cannot be given a unique name",
                          texture.name.c_str());
            return false;
        }

        // texture vectors for an arbitrary triangle, from its three normalized
        // uvs. the result is in texel units, which is what bsp texinfo expects.
        bool calc_texture_vectors(const math::vec3v p[3], const double uv[3][2],
                                  int width, int height, float vectors[2][4])
        {
            const math::vec3v e1 = p[1] - p[0];
            const math::vec3v e2 = p[2] - p[0];
            const math::vec3v normal = math::cross(e1, e2);

            const double ds1 = (uv[1][0] - uv[0][0]) * width;
            const double ds2 = (uv[2][0] - uv[0][0]) * width;
            const double dt1 = (uv[1][1] - uv[0][1]) * height;
            const double dt2 = (uv[2][1] - uv[0][1]) * height;

            const math::vec3v cross23 = math::cross(e2, normal);
            const math::vec3v cross31 = math::cross(normal, e1);
            const double det = math::dot(e1, cross23);
            if (std::fabs(det) < 1e-9)
                return false;

            math::vec3v s, t;
            for (int axis = 0; axis < 3; axis++)
            {
                s[axis] = (ds1 * cross23[axis] + ds2 * cross31[axis]) / det;
                t[axis] = (dt1 * cross23[axis] + dt2 * cross31[axis]) / det;
            }
            if (math::length(math::cross(s, t)) < 1e-6)
                return false;

            for (int axis = 0; axis < 3; axis++)
            {
                vectors[0][axis] = (float)s[axis];
                vectors[1][axis] = (float)t[axis];
            }
            vectors[0][3] = (float)(uv[0][0] * width - math::dot(p[0], s));
            vectors[1][3] = (float)(uv[0][1] * height - math::dot(p[0], t));
            return true;
        }

        // when the uvs collapse, fall back to an axial projection along the
        // dominant normal axis instead of dropping the triangle
        bool calc_fallback_texture_vectors(const math::vec3v p[3], const double uv[3][2],
                                           int width, int height, float vectors[2][4])
        {
            const math::vec3v normal = math::cross(p[1] - p[0], p[2] - p[0]);
            const double length = math::length(normal);
            if (length < 1e-9)
                return false;
            const math::vec3v unit = normal * (1.0 / length);

            math::vec3v reference;
            reference[std::fabs(unit[2]) < 0.9 ? 2 : 1] = 1.0;
            math::vec3v s = math::cross(reference, unit);
            const double s_length = math::length(s);
            if (s_length < 1e-9)
                return false;
            s = s * (1.0 / s_length);
            math::vec3v t = math::cross(unit, s);
            const double t_length = math::length(t);
            if (t_length < 1e-9)
                return false;
            t = t * (1.0 / t_length);

            for (int axis = 0; axis < 3; axis++)
            {
                vectors[0][axis] = (float)s[axis];
                vectors[1][axis] = (float)t[axis];
            }
            vectors[0][3] = (float)(uv[0][0] * width - math::dot(p[0], s));
            vectors[1][3] = (float)(uv[0][1] * height - math::dot(p[0], t));
            return true;
        }

        struct prism_side
        {
            math::vec3v points[3];
            std::string texture;
            float vectors[2][4] = {};
        };

        struct prism
        {
            prism_side sides[5];
        };

        std::vector<std::string> model_roots(const std::string &map_path)
        {
            std::vector<std::string> roots;
            const std::string map_directory = fs::directory(map_path);
            if (!map_directory.empty())
            {
                const std::string game_directory = fs::directory(map_directory);
                if (!game_directory.empty() && game_directory != map_directory)
                    roots.push_back(game_directory);
                roots.push_back(map_directory);
            }
            roots.push_back(".");
            return roots;
        }

        bool read_model(const std::string &map_path, const std::string &model_name,
                        std::vector<byte> &data, std::string &resolved)
        {
            std::string relative = model_name;
            for (char &c : relative)
                if (c == '\\')
                    c = '/';
            std::vector<std::string> candidates = {relative};
            if (relative.size() < 4
                || !str::iequals(relative.c_str() + relative.size() - 4, ".mdl"))
            {
                candidates.push_back(relative + ".mdl");
            }

            for (const std::string &candidate : candidates)
            {
                if (fs::exists(candidate) && fs::read_all(candidate, data))
                {
                    resolved = candidate;
                    return true;
                }
                for (const std::string &root : model_roots(map_path))
                {
                    const std::string path = root + "/" + candidate;
                    if (fs::exists(path) && fs::read_all(path, data))
                    {
                        resolved = path;
                        return true;
                    }
                }
            }
            return false;
        }

        int parse_collision_mode(const char *value)
        {
            if (!value || *value == '\0')
                return 0;
            if (std::strcmp(value, "2") == 0 || str::iequals(value, "exacthull"))
                return 2;
            if (std::strcmp(value, "3") == 0 || str::iequals(value, "exacthull_legacy")
                || str::iequals(value, "legacytrim"))
                return 3;
            if (std::strcmp(value, "1") == 0 || str::iequals(value, "yes") || str::iequals(value, "true"))
                return 1;
            return 0;
        }

        void strip_studio_keys(map_entity &entity, bool world, int collision_mode)
        {
            entity.set_value("classname", world ? "info_null" : "func_wall");
            if (world)
            {
                entity.set_value("spawnflags", "");
                entity.set_value("zhlt_lightflags", "");
            }
            else
            {
                entity.set_value("spawnflags", "0");
                char light_flags[16];
                std::snprintf(light_flags, sizeof(light_flags), "%d",
                              entity.int_value("zhlt_lightflags") | (int)opaque_light_flag);
                entity.set_value("zhlt_lightflags", light_flags);
            }
            entity.set_value("model", "");
            entity.set_value("origin", "");
            entity.set_value("angles", "");
            entity.set_value("angle", "");
            entity.set_value("body", "");
            entity.set_value("skin", "");
            entity.set_value("scale", "");
            entity.set_value("xform", "");
            entity.set_value("zhlt_studiobsp", "");
            if (collision_mode == 2)
                entity.set_value("zhlt_studiobsp_collision", "2");
            else if (collision_mode == 3)
                entity.set_value("zhlt_studiobsp_collision", "3");
            else
                entity.set_value("zhlt_studiobsp_collision", "");
            entity.set_value("zhlt_studiobsp_doublesided", "");
            entity.set_value("zhlt_studiobsp_thickness", "");
            entity.set_value("zhlt_studiobsp_world", "");
            entity.set_value("zhlt_studioshadow", "");
            entity.origin = {};
        }

        // insert generated brushes right after the target entity's existing
        // range, shifting the brush and side indices of everything after it
        void insert_brushes(map_source &map, int entity_index,
                            std::vector<map_brush> brushes, std::vector<brush_side> sides)
        {
            map_entity &entity = map.entities[(std::size_t)entity_index];
            const std::size_t brush_at = (std::size_t)entity.first_brush + entity.num_brushes;
            const std::size_t side_at = brush_at < map.brushes.size()
                ? (std::size_t)map.brushes[brush_at].first_side
                : map.sides.size();

            for (std::size_t i = 0; i < brushes.size(); i++)
            {
                brushes[i].first_side = (int)side_at + (int)(i * 5);
                brushes[i].num_sides = 5;
                brushes[i].brush_num = entity.num_brushes + (int)i;
                brushes[i].entity_num = entity_index;
            }

            map.sides.insert(map.sides.begin() + (std::ptrdiff_t)side_at, sides.begin(),
                             sides.end());
            for (map_brush &brush : map.brushes)
            {
                if (brush.first_side >= (int)side_at)
                    brush.first_side += (int)sides.size();
            }

            for (int e = 0; e < (int)map.entities.size(); e++)
            {
                // the target keeps its own first brush; its count grows below
                if (e == entity_index)
                    continue;
                map_entity &candidate = map.entities[(std::size_t)e];
                if (candidate.first_brush >= (int)brush_at)
                    candidate.first_brush += (int)brushes.size();
            }

            map.brushes.insert(map.brushes.begin() + (std::ptrdiff_t)brush_at,
                               brushes.begin(), brushes.end());
            entity.num_brushes += (int)brushes.size();
        }

        bool build_entity(map_source &map, texture_registry &registry, int entity_index,
                          int target_entity, bool world, bool bake_bsp, const std::string &map_path,
                          int &generated)
        {
            map_entity &entity = map.entities[(std::size_t)entity_index];
            const std::string model_name = entity.value("model");
            if (model_name.empty())
            {
                logging::warn("zhlt_studiobsp: entity %d has studio bsp/collision requested but no model",
                              entity_index);
                return false;
            }
            if (entity.num_brushes != 0)
            {
                logging::warn("zhlt_studiobsp: entity %d has brushes; only point entities "
                              "can bake a studio model",
                              entity_index);
                return false;
            }

            math::vec3v origin, angles, scale;
            {
                double x = 0, y = 0, z = 0;
                if (std::sscanf(entity.value("origin"), "%lf %lf %lf", &x, &y, &z) == 3)
                    origin = {x, y, z};
                if (std::sscanf(entity.value("angles"), "%lf %lf %lf", &x, &y, &z) == 3)
                    angles = {x, y, z};
                else if (std::sscanf(entity.value("angle"), "%lf", &y) == 1)
                    angles = {0, y, 0};
                angles[0] = -angles[0];

                const bool explicit_scale = *entity.value("scale") != '\0'
                    || *entity.value("xform") != '\0';
                const double uniform = std::atof(entity.value("scale"));
                if (std::sscanf(entity.value("xform"), "%lf %lf %lf", &x, &y, &z) == 3)
                    scale = {x, y, z};
                if (scale == math::vec3v{})
                    scale = {uniform, uniform, uniform};
                bool clamped = false;
                for (int axis = 0; axis < 3; axis++)
                {
                    if (!std::isfinite(scale[axis]) || scale[axis] < 0.01)
                    {
                        scale[axis] = 1.0;
                        clamped = true;
                    }
                    if (scale[axis] > 16.0)
                    {
                        scale[axis] = 16.0;
                        clamped = true;
                    }
                }
                if (clamped && explicit_scale)
                {
                    logging::warn("zhlt_studiobsp: entity %d scale was clamped to the "
                                  "supported range",
                                  entity_index);
                }
            }

            float thickness = default_thickness;
            if (*entity.value("zhlt_studiobsp_thickness") != '\0')
            {
                thickness = (float)std::atof(entity.value("zhlt_studiobsp_thickness"));
                if (!std::isfinite(thickness))
                    thickness = default_thickness;
                const float original = thickness;
                thickness = std::max(min_thickness, std::min(max_thickness, thickness));
                if (thickness != original)
                {
                    logging::warn("zhlt_studiobsp: entity %d thickness was clamped to %.2f",
                                  entity_index, (double)thickness);
                }
            }
            const bool double_sided = *entity.value("zhlt_studiobsp_doublesided") == '\0'
                || entity.int_value("zhlt_studiobsp_doublesided") != 0;
            (void)double_sided; // brush mode always needs a closed, textured solid
            const int collision_mode = parse_collision_mode(entity.value("zhlt_studiobsp_collision"));
            const bool player_collision = (collision_mode == 1);

            std::vector<byte> data;
            std::string resolved;
            if (!read_model(map_path, model_name, data, resolved))
            {
                logging::warn("zhlt_studiobsp: model '%s' not found", model_name.c_str());
                return false;
            }

            format::goldsrc_model_pose pose;
            pose.body = entity.int_value("body");
            pose.skin = entity.int_value("skin");
            format::studio_model model;
            std::string error;
            if (!format::read_goldsrc_model(data, pose, model, &error))
            {
                logging::warn("zhlt_studiobsp: model '%s': %s", resolved.c_str(),
                              error.c_str());
                return false;
            }

            if (bake_bsp)
            {
                if (model.textures.empty() && !model.meshes.empty())
                {
                    logging::warn("zhlt_studiobsp: model '%s' has no embedded skins", model_name.c_str());
                    return false;
                }
            }
            else
            {
                int max_material = 0;
                for (const format::studio_mesh &mesh : model.meshes)
                    max_material = std::max(max_material, mesh.material);

                format::studio_texture dummy_tex;
                dummy_tex.name = "zhlt_col";
                dummy_tex.image.width = 16;
                dummy_tex.image.height = 16;
                dummy_tex.image.pixels.assign(256, 0);
                dummy_tex.image.palette = {};

                if ((int)model.textures.size() <= max_material)
                    model.textures.resize((std::size_t)(max_material + 1), dummy_tex);
                for (format::studio_texture &tex : model.textures)
                {
                    tex.image.width = 16;
                    tex.image.height = 16;
                    tex.image.pixels.assign(256, 0);
                    tex.image.palette = {};
                }
            }

            std::vector<std::string> texture_names(model.textures.size());
            for (std::size_t i = 0; i < model.textures.size(); i++)
            {
                if (!register_texture(registry, model.textures[i], texture_names[i]))
                    return false;
            }

            const placement place_matrix = placement_for(origin, angles, scale);
            std::vector<prism> prisms;
            int degenerate = 0;
            int inflated = 0;
            int fallback_mappings = 0;
            for (const format::studio_mesh &mesh : model.meshes)
            {
                if (mesh.material < 0
                    || (std::size_t)mesh.material >= model.textures.size())
                    continue;
                const format::studio_texture &texture =
                    model.textures[(std::size_t)mesh.material];
                const int width = (int)texture.image.width;
                const int height = (int)texture.image.height;

                for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
                {
                    math::vec3v points[3];
                    double uv[3][2];
                    for (int k = 0; k < 3; k++)
                    {
                        const format::studio_vertex &vertex =
                            model.vertices[(std::size_t)mesh.indices[t + (std::size_t)k]];
                        points[k] = place(place_matrix, vertex.position);
                        uv[k][0] = vertex.u;
                        uv[k][1] = vertex.v;
                    }

                    math::vec3v normal = math::cross(points[1] - points[0],
                                                     points[2] - points[0]);
                    double length = math::length(normal);
                    if (length < 1e-9)
                    {
                        degenerate++;
                        continue;
                    }

                    // a heavily downscaled model can have triangles thinner than
                    // the csg plane epsilon; those wedges never form a closed
                    // brush. inflate just the slivers in their own plane so the
                    // prism keeps its front plane, position and winding order.
                    const double e0 = math::length(points[1] - points[0]);
                    const double e1 = math::length(points[2] - points[1]);
                    const double e2 = math::length(points[0] - points[2]);
                    const double max_edge = std::max(e0, std::max(e1, e2));
                    const double altitude = max_edge > 0.0 ? length / max_edge : 0.0;
                    if (altitude < minimum_bake_altitude)
                    {
                        const double factor = std::min(64.0,
                            minimum_bake_altitude / std::max(altitude, 1e-9));
                        const math::vec3v center = (points[0] + points[1] + points[2])
                                                   * (1.0 / 3.0);
                        for (int k = 0; k < 3; k++)
                            points[k] = center + (points[k] - center) * factor;
                        normal = math::cross(points[1] - points[0],
                                             points[2] - points[0]);
                        length = math::length(normal);
                        if (length < 1e-9)
                        {
                            degenerate++;
                            continue;
                        }
                        inflated++;
                    }
                    const math::vec3v unit = normal * (1.0 / length);
                    math::vec3v back[3];
                    for (int k = 0; k < 3; k++)
                        back[k] = points[k] - unit * (double)thickness;

                    prism p;
                    p.sides[0].points[0] = points[2];                    p.sides[0].points[1] = points[1];
                    p.sides[0].points[2] = points[0];
                    p.sides[0].texture = texture_names[(std::size_t)mesh.material];
                    if (!calc_texture_vectors(points, uv, width, height,
                                              p.sides[0].vectors))
                    {
                        if (!calc_fallback_texture_vectors(points, uv, width, height,
                                                           p.sides[0].vectors))
                        {
                            degenerate++;
                            continue;
                        }
                        fallback_mappings++;
                    }

                    p.sides[1].points[0] = back[0];
                    p.sides[1].points[1] = back[1];
                    p.sides[1].points[2] = back[2];
                    p.sides[1].texture = p.sides[0].texture;
                    std::memcpy(p.sides[1].vectors, p.sides[0].vectors,
                                sizeof(p.sides[1].vectors));

                    for (int edge = 0; edge < 3; edge++)
                    {
                        const int next = (edge + 1) % 3;
                        prism_side &side = p.sides[2 + edge];
                        side.points[0] = points[edge];
                        side.points[1] = points[next];
                        side.points[2] = back[next];
                        // a side face is perpendicular to the triangle normal,
                        // so the front projection would give a zero texnormal
                        // in rad. project it axially on its own plane instead.
                        side.texture = p.sides[0].texture;
                        const double side_uv[3][2] = {{0, 0}, {1, 0}, {0, 1}};
                        calc_fallback_texture_vectors(side.points, side_uv, width, height,
                                                      side.vectors);
                    }

                    prisms.push_back(std::move(p));
                }
            }

            if (prisms.empty())
            {
                logging::warn("zhlt_studiobsp: model '%s' produced no triangles",
                              model_name.c_str());
                return false;
            }
            if (prisms.size() > (std::size_t)0x7fffffff / 5)
            {
                logging::warn("zhlt_studiobsp: model '%s' has too many triangles",
                              model_name.c_str());
                return false;
            }

            std::vector<map_brush> brushes;
            std::vector<brush_side> sides;
            brushes.reserve(prisms.size());
            sides.reserve(prisms.size() * 5);
            for (std::size_t i = 0; i < prisms.size(); i++)
            {
                map_brush brush;
                brush.original_entity_num = entity_index;
                brush.original_brush_num = (int)i;
                brush.entity_num = target_entity;
                brush.num_sides = 5;
                brush.noclip = !player_collision;
                brush.detail_level = 1;
                brush.clipnode_detail_level = 1;
                brush.generated_studio = true;
                brush.contents = content::solid;
                brushes.push_back(brush);

                for (int s = 0; s < 5; s++)
                {
                    const prism_side &source = prisms[i].sides[s];
                    brush_side side;
                    for (int k = 0; k < 3; k++)
                        side.planepts[k] = source.points[k];
                    std::memcpy(side.texture.name, source.texture.c_str(),
                                std::min(source.texture.size(),
                                         sizeof(side.texture.name) - 1));
                    side.texture.txcommand = 'Q';
                    for (int axis = 0; axis < 3; axis++)
                    {
                        side.texture.quark.vecs[0][axis] = source.vectors[0][axis];
                        side.texture.quark.vecs[1][axis] = source.vectors[1][axis];
                    }
                    side.texture.quark.vecs[0][3] = source.vectors[0][3];
                    side.texture.quark.vecs[1][3] = source.vectors[1][3];
                    sides.push_back(side);
                }
            }

            const std::size_t count = brushes.size();
            insert_brushes(map, target_entity, std::move(brushes), std::move(sides));
            generated += (int)count;

            if (degenerate)
            {
                logging::warn("zhlt_studiobsp: %s skipped %d degenerate triangle(s)",
                              model_name.c_str(), degenerate);
            }
            if (inflated)
            {
                logging::warn("zhlt_studiobsp: %s inflated %d sliver triangle(s) to %.2f "
                              "world units",
                              model_name.c_str(), inflated, (double)minimum_bake_altitude);
            }
            if (fallback_mappings)
            {
                logging::warn("zhlt_studiobsp: %s used planar fallback mapping for %d "
                              "triangle(s)",
                              model_name.c_str(), fallback_mappings);
            }
            if (bake_bsp)
            {
                logging::info("  %-14s %d brushes (%s)\n", "studiobsp", (int)count,
                              world ? "world" : "func_wall");
            }
            else
            {
                logging::info("  %-14s %d collision brushes (func_wall)\n", "studiobsp", (int)count);
            }
            return true;
        }
    }

    std::vector<format::mip_texture> inject_studio_brushes(map_source &map,
                                                           const std::string &map_path)
    {
        texture_registry registry;
        for (const brush_side &side : map.sides)
            registry.map_names.insert(uppercase(side.texture.name));

        int generated = 0;
        const int original_entity_count = (int)map.entities.size();
        for (int i = 1; i < original_entity_count; i++)
        {
            const char *studiobsp_raw = map.entities[(std::size_t)i].value("zhlt_studiobsp");
            int bake_mode = map.entities[(std::size_t)i].int_value("zhlt_studiobsp");
            if (str::iequals(studiobsp_raw, "collision_only") || str::iequals(studiobsp_raw, "nodraw")
                || str::iequals(studiobsp_raw, "invisible")
                || map.entities[(std::size_t)i].int_value("zhlt_studiobsp_collision_only") != 0
                || map.entities[(std::size_t)i].int_value("zhlt_studiobsp_nodraw") != 0)
            {
                bake_mode = 2;
            }

            int collision_mode =
                parse_collision_mode(map.entities[(std::size_t)i].value("zhlt_studiobsp_collision"));

            if (bake_mode == 2 && collision_mode == 0)
                collision_mode = 2;

            if (bake_mode == 0 && collision_mode == 0)
                continue;

            if (bake_mode == 1)
            {
                bool world = map.entities[(std::size_t)i].int_value("zhlt_studiobsp_world") != 0;
                if (world && (collision_mode == 2 || collision_mode == 3))
                {
                    logging::warn("zhlt_studiobsp: entity %d: ExactHull collision requires a submodel; forcing world = 0 (func_wall)\n", i);
                    world = false;
                }
                const bool built = build_entity(map, registry, i, world ? 0 : i, world, true,
                                                map_path, generated);
                if (!built)
                    continue;

                map_entity &entity = map.entities[(std::size_t)i];
                strip_studio_keys(entity, world, collision_mode);
                if (world)
                    entity.num_brushes = 0;
            }
            else if (bake_mode == 2)
            {
                if (map.entities[(std::size_t)i].int_value("zhlt_studiobsp_world") != 0)
                {
                    logging::warn("zhlt_studiobsp: entity %d: collision proxy requires a submodel; ignoring zhlt_studiobsp_world\n", i);
                }

                const bool built = build_entity(map, registry, i, i, false, false,
                                                map_path, generated);
                if (!built)
                    continue;

                map_entity &entity = map.entities[(std::size_t)i];
                strip_studio_keys(entity, false, collision_mode);
                entity.set_value("zhlt_collision_only", "1");
                entity.set_value("zhlt_lightflags", "");
            }
            else
            {
                if (map.entities[(std::size_t)i].int_value("zhlt_studiobsp_world") != 0)
                {
                    logging::warn("zhlt_studiobsp: entity %d: decoupled studio model collision requires a submodel; ignoring zhlt_studiobsp_world\n", i);
                }

                map_entity col_ent;
                col_ent.set_value("classname", "func_wall");
                col_ent.set_value("zhlt_collision_only", "1");
                if (collision_mode == 2)
                    col_ent.set_value("zhlt_studiobsp_collision", "2");
                else if (collision_mode == 3)
                    col_ent.set_value("zhlt_studiobsp_collision", "3");
                else
                    col_ent.set_value("zhlt_studiobsp_collision", "1");

                col_ent.first_brush = (int)map.brushes.size();
                col_ent.num_brushes = 0;
                const int target_entity = (int)map.entities.size();
                map.entities.push_back(std::move(col_ent));

                const bool built = build_entity(map, registry, i, target_entity, false, false,
                                                map_path, generated);
                if (!built)
                {
                    map.entities.pop_back();
                    continue;
                }

                // Strip studiobsp-specific keys from original entity so downstream CSG won't touch them,
                // but keep original classname, model, origin, angles, rendermode, zhlt_studioshadow, etc.!
                map_entity &orig_ent = map.entities[(std::size_t)i];
                orig_ent.set_value("zhlt_studiobsp", "");
                orig_ent.set_value("zhlt_studiobsp_collision", "");
                orig_ent.set_value("zhlt_studiobsp_doublesided", "");
                orig_ent.set_value("zhlt_studiobsp_thickness", "");
                orig_ent.set_value("zhlt_studiobsp_world", "");
            }
        }

        if (generated)
        {
            logging::info("  %-14s %d triangle prisms from studio models\n", "studio bsp",
                          generated);
        }
        return registry.order;
    }
}
