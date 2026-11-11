#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../common/filesystem.h"
#include "../common/log.h"
#include "../common/string_util.h"
#include "../format/mdl/goldsrc/read.h"
#include "internal.h"
#include "studio_facet.h"
#include "studio_simplify.h"

// opaque studio model (mdl) shadows: models named by env_static entities or
// entities with zhlt_studioshadow block light like opaque brush entities.
//
// the reference builds an axis aligned tree over the posed triangles and traces
// them with three different tests selected by zhlt_shadowmode. the tree only
// changes the iteration order, so this port walks the triangle list directly
// and keeps the tolerance based hit test, which is what decides the lighting.

namespace rad
{
    namespace
    {
        constexpr int shadow_fast = 0;   // per triangle bounds only
        constexpr int shadow_normal = 1; // per triangle, alpha tested
        constexpr int shadow_slow = 2;   // simplified mesh, expanded facets

        constexpr vec_t bary_epsilon = 0.01f;
        constexpr vec_t coplanar_epsilon = 0.25f;
        constexpr vec_t near_shadow_epsilon = 1.5f;
        constexpr vec_t seam_epsilon = 0.0001f;
        constexpr vec_t trace_expand = 1.0f;

        struct placement
        {
            float m[3][4];
        };

        // the reference AngleMatrix for an entity, including the per axis
        // xform scale. angles are degrees and the caller already applied the
        // negative pitch quirk.
        placement placement_for(const vec3v &origin, const vec3v &angles,
                                const vec3v &scale)
        {
            const double to_radians = math::pi * 2.0 / 360.0;
            const double sy = std::sin((double)angles[1] * to_radians);
            const double cy = std::cos((double)angles[1] * to_radians);
            const double sp = std::sin((double)angles[0] * to_radians);
            const double cp = std::cos((double)angles[0] * to_radians);
            const double sr = std::sin((double)angles[2] * to_radians);
            const double cr = std::cos((double)angles[2] * to_radians);

            placement out{};
            out.m[0][0] = (float)(cp * cy * scale[0]);
            out.m[1][0] = (float)(cp * sy * scale[0]);
            out.m[2][0] = (float)(-sp * scale[0]);
            out.m[0][1] = (float)((sr * sp * cy + cr * -sy) * scale[1]);
            out.m[1][1] = (float)((sr * sp * sy + cr * cy) * scale[1]);
            out.m[2][1] = (float)(sr * cp * scale[1]);
            out.m[0][2] = (float)((cr * sp * cy + -sr * -sy) * scale[2]);
            out.m[1][2] = (float)((cr * sp * sy + -sr * cy) * scale[2]);
            out.m[2][2] = (float)(cr * cp * scale[2]);
            out.m[0][3] = origin.x;
            out.m[1][3] = origin.y;
            out.m[2][3] = origin.z;
            return out;
        }

        vec3v transform_point(const placement &p, const float in[3])
        {
            return vec3v{
                p.m[0][0] * in[0] + p.m[0][1] * in[1] + p.m[0][2] * in[2] + p.m[0][3],
                p.m[1][0] * in[0] + p.m[1][1] * in[1] + p.m[1][2] * in[2] + p.m[1][3],
                p.m[2][0] * in[0] + p.m[2][1] * in[1] + p.m[2][2] * in[2] + p.m[2][3]};
        }

        std::string normalize_slashes(std::string path)
        {
            for (char &c : path)
                if (c == '\\')
                    c = '/';
            return path;
        }

        bool has_model_extension(const std::string &path)
        {
            return path.size() >= 4 && str::iequals(path.c_str() + path.size() - 4, ".mdl");
        }

        // the reference resolves models against the game directory, the parent
        // of the map's directory, then falls back to the working directory. the
        // map directory itself and -waddir are accepted as extra convenience.
        std::vector<std::string> model_search_roots(const rad_state &state)
        {
            std::vector<std::string> roots;
            const std::string map_directory = fs::directory(state.base_path);
            if (!map_directory.empty())
            {
                const std::string game_directory = fs::directory(map_directory);
                if (!game_directory.empty() && game_directory != map_directory)
                    roots.push_back(game_directory);
                roots.push_back(map_directory);
            }
            roots.push_back(".");
            for (const std::string &folder : state.options.wad_folders)
                roots.push_back(folder);
            return roots;
        }

        bool load_model_bytes(const rad_state &state, const std::string &model_path,
                              std::vector<byte> &data, std::string &resolved)
        {
            const std::string relative = normalize_slashes(model_path);
            const std::vector<std::string> candidates = [&]() {
                std::vector<std::string> out;
                out.push_back(relative);
                if (!has_model_extension(relative))
                    out.push_back(relative + ".mdl");
                return out;
            }();

            for (const std::string &candidate : candidates)
            {
                if (fs::exists(candidate) && fs::read_all(candidate, data))
                {
                    resolved = candidate;
                    return true;
                }
                for (const std::string &root : model_search_roots(state))
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

        constexpr vec_t plane_normal_epsilon = 0.00001f;
        constexpr vec_t plane_dist_epsilon = 0.04f;
        constexpr vec_t facet_frac_epsilon = 1.0f / 32.0f;
        constexpr int max_facet_planes = 32;

        void snap_vector(vec3v &normal)
        {
            for (int i = 0; i < 3; i++)
            {
                if (std::fabs(normal[i] - 1.0f) < plane_normal_epsilon)
                {
                    normal = vec3v{};
                    normal[i] = 1.0f;
                    return;
                }
                if (std::fabs(normal[i] + 1.0f) < plane_normal_epsilon)
                {
                    normal = vec3v{};
                    normal[i] = -1.0f;
                    return;
                }
            }
        }

        void snap_plane(studio_shadow_plane &plane)
        {
            snap_vector(plane.normal);
            const vec_t rounded = (vec_t)std::round((double)plane.dist);
            if (std::fabs(plane.dist - rounded) < plane_dist_epsilon)
                plane.dist = rounded;
        }

        void categorize_plane(studio_shadow_plane &plane)
        {
            plane.signbits = 0;
            plane.type = 3;
            for (int i = 0; i < 3; i++)
            {
                if (plane.normal[i] < 0.0f)
                {
                    plane.signbits |= 1 << i;
                    if (plane.normal[i] == -1.0f)
                    {
                        plane.signbits = 1 << i;
                        plane.normal = vec3v{};
                        plane.normal[i] = -1.0f;
                        break;
                    }
                }
                else if (plane.normal[i] == 1.0f)
                {
                    plane.type = i;
                    plane.signbits = 0;
                    plane.normal = vec3v{};
                    plane.normal[i] = 1.0f;
                    break;
                }
            }
        }

        bool same_plane(const studio_shadow_plane &plane, const vec3v &normal, vec_t dist)
        {
            return std::fabs(plane.normal[0] - normal[0]) < plane_normal_epsilon
                && std::fabs(plane.normal[1] - normal[1]) < plane_normal_epsilon
                && std::fabs(plane.normal[2] - normal[2]) < plane_normal_epsilon
                && std::fabs(plane.dist - dist) < plane_dist_epsilon;
        }

        // the reference's AddMeshTrinagle facet: front plane, the six axial
        // bounds and edge bevels for every non axial edge
        bool facet_planes(const vec3v p[3], vec3v mins, vec3v maxs,
                          std::vector<studio_shadow_plane> &planes)
        {
            math::vec3v v1 = p[1] - p[0];
            math::vec3v v2 = p[2] - p[0];
            math::vec3v normal = math::cross(v2, v1);
            const double length = math::length(normal);
            if (length == 0.0)
                return false;
            normal = normal * (vec_t)(1.0 / length);
            studio_shadow_plane main;
            main.normal = normal;
            main.dist = (vec_t)math::dot(p[0], normal);
            snap_plane(main);
            planes.push_back(main);

            for (int axis = 0; axis < 3; axis++)
            {
                for (int direction = -1; direction <= 1; direction += 2)
                {
                    bool present = false;
                    for (const studio_shadow_plane &plane : planes)
                        present = present || plane.normal[axis] == (vec_t)direction;
                    if (present)
                        continue;
                    vec3v axis_normal{};
                    axis_normal[axis] = (vec_t)direction;
                    studio_shadow_plane plane;
                    plane.normal = axis_normal;
                    plane.dist = direction == 1 ? maxs[axis] : -mins[axis];
                    planes.push_back(plane);
                }
            }

            for (int i = 0; i < 3; i++)
            {
                const int j = (i + 1) % 3;
                math::vec3v edge = p[i] - p[j];
                if (math::length(edge) < 0.5)
                    continue;
                const double edge_length = math::length(edge);
                edge = edge * (vec_t)(1.0 / edge_length);
                snap_vector(edge);
                bool axial = false;
                for (int axis = 0; axis < 3; axis++)
                    axial = axial || edge[axis] == 1.0f || edge[axis] == -1.0f;
                if (axial)
                    continue;

                for (int axis = 0; axis < 3; axis++)
                {
                    for (int direction = -1; direction <= 1; direction += 2)
                    {
                        vec3v reference{};
                        reference[axis] = (vec_t)direction;
                        math::vec3v bevel = math::cross(edge, reference);
                        const double bevel_length = math::length(bevel);
                        if (bevel_length < 0.5)
                            continue;
                        bevel = bevel * (vec_t)(1.0 / bevel_length);
                        const vec_t dist = (vec_t)math::dot(p[i], bevel);

                        bool duplicate = false;
                        for (const studio_shadow_plane &plane : planes)
                            duplicate = duplicate || same_plane(plane, bevel, dist);
                        if (duplicate)
                            continue;

                        bool outer = true;
                        for (int k = 0; k < 3 && outer; k++)
                        {
                            if (k == i)
                                continue;
                            if (math::dot(p[k], bevel) - dist > 0.1)
                                outer = false;
                        }
                        if (!outer)
                            continue;

                        studio_shadow_plane plane;
                        plane.normal = bevel;
                        plane.dist = dist;
                        planes.push_back(plane);
                    }
                }
            }
            return true;
        }

        void build_facets(studio_shadow_model &model,
                          const std::vector<vec3v> &positions,
                          const std::vector<std::array<int, 3>> &triangles)
        {
            model.facets.clear();
            model.planes.clear();
            for (const std::array<int, 3> &indices : triangles)
            {
                vec3v p[3];
                vec3v mins{1e30f, 1e30f, 1e30f};
                vec3v maxs{-1e30f, -1e30f, -1e30f};
                bool valid = true;
                for (int k = 0; k < 3; k++)
                {
                    if (indices[k] < 0 || (std::size_t)indices[k] >= positions.size())
                    {
                        valid = false;
                        break;
                    }
                    p[k] = positions[(std::size_t)indices[k]];
                    for (int axis = 0; axis < 3; axis++)
                    {
                        mins[axis] = std::min(mins[axis], p[k][axis]);
                        maxs[axis] = std::max(maxs[axis], p[k][axis]);
                    }
                }
                if (!valid)
                    continue;

                std::vector<studio_shadow_plane> planes;
                planes.reserve(max_facet_planes);
                if (!facet_planes(p, mins, maxs, planes) || planes.empty())
                    continue;

                studio_shadow_facet facet;
                facet.mins = mins;
                facet.maxs = maxs;
                facet.first_plane = (int)model.planes.size();
                facet.num_planes = (int)planes.size();
                for (studio_shadow_plane &plane : planes)
                {
                    snap_plane(plane);
                    categorize_plane(plane);
                    model.planes.push_back(plane);
                }
                model.facets.push_back(facet);

                studio_shadow_triangle triangle;
                triangle.texture = -1;
                triangle.mins = mins;
                triangle.maxs = maxs;
                for (int k = 0; k < 3; k++)
                {
                    triangle.p[k] = p[k];
                    triangle.uv[k] = vec3v{};
                }
                model.triangles.push_back(triangle);
            }
        }

        studio_shadow_model build_shadow_model(const format::studio_model &source,
                                               const placement &place, int trace_mode)
        {
            studio_shadow_model out;
            out.trace_mode = trace_mode;
            out.textures = source.textures;
            out.mins = vec3v{1e30f, 1e30f, 1e30f};
            out.maxs = vec3v{-1e30f, -1e30f, -1e30f};

            if (trace_mode == shadow_slow)
            {
                // the reference simplifies the posed mesh first and then tests
                // the expanded facets, which ignore skins entirely
                std::vector<vec3v> positions;
                positions.reserve(source.vertices.size());
                for (const format::studio_vertex &vertex : source.vertices)
                    positions.push_back(transform_point(place, vertex.position));

                std::vector<std::array<int, 3>> triangles;
                for (const format::studio_mesh &mesh : source.meshes)
                {
                    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
                    {
                        triangles.push_back({mesh.indices[t], mesh.indices[t + 1],
                                             mesh.indices[t + 2]});
                    }
                }

                const simplified_mesh simplified =
                    simplify_shadow_mesh(positions, triangles);
                if (simplified.positions.size() != positions.size())
                {
                    logging::info("  %-14s %s: %d -> %d verts, %d tris\n", "simplified",
                                  source.name.c_str(), (int)positions.size(),
                                  (int)simplified.positions.size(),
                                  (int)simplified.triangles.size());
                }
                build_facets(out, simplified.positions, simplified.triangles);
                for (const studio_shadow_triangle &triangle : out.triangles)
                {
                    for (int k = 0; k < 3; k++)
                    {
                        for (int axis = 0; axis < 3; axis++)
                        {
                            out.mins[axis] = std::min(out.mins[axis], triangle.p[k][axis]);
                            out.maxs[axis] = std::max(out.maxs[axis], triangle.p[k][axis]);
                        }
                    }
                }
                if (out.triangles.empty())
                {
                    out.mins = vec3v{};
                    out.maxs = vec3v{};
                }
                return out;
            }

            for (const format::studio_mesh &mesh : source.meshes)
            {
                for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
                {
                    studio_shadow_triangle triangle;
                    triangle.texture = mesh.material;
                    triangle.mins = vec3v{1e30f, 1e30f, 1e30f};
                    triangle.maxs = vec3v{-1e30f, -1e30f, -1e30f};
                    for (int k = 0; k < 3; k++)
                    {
                        const format::studio_vertex &vertex =
                            source.vertices[(std::size_t)mesh.indices[t + (std::size_t)k]];
                        triangle.p[k] = transform_point(place, vertex.position);
                        triangle.uv[k] = vec3v{vertex.u, vertex.v, 0.0f};
                        for (int axis = 0; axis < 3; axis++)
                        {
                            triangle.mins[axis] = std::min(triangle.mins[axis], triangle.p[k][axis]);
                            triangle.maxs[axis] = std::max(triangle.maxs[axis], triangle.p[k][axis]);
                            out.mins[axis] = std::min(out.mins[axis], triangle.p[k][axis]);
                            out.maxs[axis] = std::max(out.maxs[axis], triangle.p[k][axis]);
                        }
                    }
                    out.triangles.push_back(triangle);
                }
            }

            if (out.triangles.empty())
            {
                out.mins = vec3v{};
                out.maxs = vec3v{};
            }
            return out;
        }

        bool bounds_overlap(const vec3v &mins1, const vec3v &maxs1, const vec3v &mins2,
                            const vec3v &maxs2)
        {
            for (int i = 0; i < 3; i++)
                if (mins1[i] > maxs2[i] || maxs1[i] < mins2[i])
                    return false;
            return true;
        }

        void segment_bounds(const vec3v &p1, const vec3v &p2, vec3v &mins, vec3v &maxs)
        {
            for (int i = 0; i < 3; i++)
            {
                mins[i] = std::min(p1[i], p2[i]) - trace_expand;
                maxs[i] = std::max(p1[i], p2[i]) + trace_expand;
            }
        }

        // swept segment against an axis aligned box, used by the fast mode
        bool clip_ray_to_box(const vec3v &start, const vec3v &end, const vec3v &mins,
                             const vec3v &maxs)
        {
            vec_t enter = 0.0f;
            vec_t leave = 1.0f;
            for (int axis = 0; axis < 3; axis++)
            {
                const vec_t delta = end[axis] - start[axis];
                if (std::fabs(delta) < 1e-8f)
                {
                    if (start[axis] < mins[axis] || start[axis] > maxs[axis])
                        return false;
                    continue;
                }
                const vec_t inv = 1.0f / delta;
                vec_t t1 = (mins[axis] - start[axis]) * inv;
                vec_t t2 = (maxs[axis] - start[axis]) * inv;
                if (t1 > t2)
                    std::swap(t1, t2);
                enter = std::max(enter, t1);
                leave = std::min(leave, t2);
                if (enter > leave)
                    return false;
            }
            return leave >= 0.0f;
        }

        // ray against one expanded facet (reference ClipRayToFacet). the trace
        // hull is a point, so the corner pushes collapse to a plain dot product
        bool clip_ray_to_facet(const studio_shadow_model &model,
                               const studio_shadow_facet &facet, const vec3v &start,
                               const vec3v &end)
        {
            double enter = -1.0;
            double leave = 1.0;
            bool start_outside = false;
            for (int i = 0; i < facet.num_planes; i++)
            {
                const studio_shadow_plane &plane =
                    model.planes[(std::size_t)(facet.first_plane + i)];
                const double d1 = math::dot(start, plane.normal) - plane.dist;
                const double d2 = math::dot(end, plane.normal) - plane.dist;
                if (d1 > 0.0)
                    start_outside = true;
                if (d1 > 0.0 && d2 >= d1)
                    return false;
                if (d1 <= 0.0 && d2 <= 0.0)
                    continue;
                const double d = 1.0 / (d1 - d2);
                const double f = d1 * d;
                if (d > 0.0)
                {
                    if (f > enter)
                        enter = f;
                }
                else if (d < 0.0)
                {
                    if (f < leave)
                        leave = f;
                }
            }
            if (!start_outside)
                return true;
            return (enter - (double)facet_frac_epsilon) <= leave && enter > -1.0
                && enter < 1.0;
        }

        // shared masked texture test at a hit point. the palette's last index is
        // the punched out pixel for both goldsrc masked textures and the
        // reference's transparent flag.
        bool triangle_blocks(const studio_shadow_model &model,
                             const studio_shadow_triangle &triangle, vec_t u, vec_t v)
        {
            if (triangle.texture < 0
                || (std::size_t)triangle.texture >= model.textures.size())
                return true;
            const format::studio_texture &texture = model.textures[(std::size_t)triangle.texture];
            const unsigned flags = (unsigned)texture.flags;
            if (flags & (unsigned)format::studio_nf_additive)
                return false;
            if (!(flags & (unsigned)format::studio_nf_masked))
                return true;
            if (texture.image.pixels.empty() || texture.image.width == 0
                || texture.image.height == 0)
                return true;

            // avoid double shadows near triangle seams on alpha tested faces
            if (u < -seam_epsilon || u > 1.0f + seam_epsilon || v < -seam_epsilon
                || (u + v) > 1.0f + seam_epsilon)
                return false;

            const vec_t w = 1.0f - (u + v);
            const vec_t s = w * triangle.uv[0].x + u * triangle.uv[1].x
                + v * triangle.uv[2].x;
            const vec_t t = w * triangle.uv[0].y + u * triangle.uv[1].y
                + v * triangle.uv[2].y;
            const vec_t wrapped_s = (vec_t)(s - std::floor(s));
            const vec_t wrapped_t = (vec_t)(t - std::floor(t));
            int x = (int)(wrapped_s * (vec_t)texture.image.width);
            int y = (int)(wrapped_t * (vec_t)texture.image.height);
            x = std::max(0, std::min(x, (int)texture.image.width - 1));
            y = std::max(0, std::min(y, (int)texture.image.height - 1));
            const std::size_t at = (std::size_t)y * texture.image.width + (std::size_t)x;
            return texture.image.pixels[at] != 0xFF;
        }

        // the reference per triangle test: moeller trumbore with its wide
        // tolerances, a near plane so a surface does not shadow itself, and the
        // masked pixel check
        bool clip_ray_to_triangle(const studio_shadow_model &model,
                                  const studio_shadow_triangle &triangle, const vec3v &start,
                                  const vec3v &direction, vec_t distance)
        {
            const vec3v edge1 = triangle.p[1] - triangle.p[0];
            const vec3v edge2 = triangle.p[2] - triangle.p[0];
            const vec3v pvec = math::cross(direction, edge2);
            const vec_t det = math::dot(edge1, pvec);
            if (std::fabs(det) < coplanar_epsilon)
                return false;

            const vec_t inv_det = 1.0f / det;
            const vec3v tvec = start - triangle.p[0];
            const vec_t u = math::dot(tvec, pvec) * inv_det;
            if (u < -bary_epsilon || u > 1.0f + bary_epsilon)
                return false;

            const vec3v qvec = math::cross(tvec, edge1);
            const vec_t v = math::dot(direction, qvec) * inv_det;
            if (v < -bary_epsilon || (u + v) > 1.0f + bary_epsilon)
                return false;

            const vec_t depth = math::dot(edge2, qvec) * inv_det;
            if (depth <= near_shadow_epsilon || depth >= distance)
                return false;

            return triangle_blocks(model, triangle, u, v);
        }
    }

    void load_studio_models(rad_state &state)
    {
        state.studio_models.clear();
        state.num_studio_models = 0;

        if (!state.options.studioshadow)
            return;

        for (std::size_t i = 0; i < state.entities.size(); i++)
        {
            const format::entity &entity = state.entities[i];
            const char *classname = entity.value("classname");
            const bool env_static = str::iequals(classname, "env_static");

            if (env_static)
            {
                // spawnflag 4 turns the shadow off for this model
                if (int_for_key(entity, "spawnflags") & 4)
                    continue;
            }
            else if (!int_for_key(entity, "zhlt_studioshadow"))
            {
                continue;
            }

            const char *model_key = entity.value("model");
            if (!model_key || !*model_key)
                continue;

            vec3v origin, angles, scale;
            vector_for_key(entity, "origin", origin);
            vector_for_key(entity, "angles", angles);
            angles[0] = -angles[0]; // the engine's pitch convention

            vec3v xform;
            vector_for_key(entity, "xform", xform);
            const vec_t uniform = float_for_key(entity, "scale");
            if (xform == vec3v{})
                xform = vec3v{uniform, uniform, uniform};
            for (int axis = 0; axis < 3; axis++)
            {
                if (xform[axis] < 0.01f)
                    xform[axis] = 1.0f;
                if (xform[axis] > 16.0f)
                    xform[axis] = 16.0f;
            }

            int trace_mode = shadow_normal;
            const char *mode_value = entity.value("zhlt_shadowmode");
            if (mode_value && *mode_value)
                trace_mode = int_for_key(entity, "zhlt_shadowmode");
            if (trace_mode < shadow_fast || trace_mode > shadow_slow)
                trace_mode = shadow_normal;

            std::vector<byte> data;
            std::string resolved;
            if (!load_model_bytes(state, model_key, data, resolved))
            {
                logging::warn("studio shadow model '%s' not found", model_key);
                continue;
            }

            format::goldsrc_model_pose pose;
            pose.body = int_for_key(entity, "body");
            pose.skin = int_for_key(entity, "skin");
            format::studio_model neutral;
            std::string error;
            if (!format::read_goldsrc_model(data, pose, neutral, &error))
            {
                logging::warn("studio shadow model '%s': %s", resolved.c_str(),
                              error.c_str());
                continue;
            }

            const placement place = placement_for(origin, angles, xform);
            studio_shadow_model shadow = build_shadow_model(neutral, place, trace_mode);
            if (shadow.triangles.empty())
                continue;
            state.studio_models.push_back(std::move(shadow));
        }

        state.num_studio_models = (int)state.studio_models.size();
        if (state.num_studio_models)
        {
            logging::info("  %-14s %d opaque studio models\n", "studio",
                          state.num_studio_models);
        }
    }

    void free_studio_models(rad_state &state)
    {
        state.studio_models.clear();
        state.num_studio_models = 0;
    }

    bool test_segment_against_studio_model(const studio_shadow_model &model,
                                           const vec3v &p1, const vec3v &p2)
    {
        if (model.triangles.empty())
            return false;

        vec3v bounds_min, bounds_max;
        segment_bounds(p1, p2, bounds_min, bounds_max);
        if (!bounds_overlap(model.mins, model.maxs, bounds_min, bounds_max))
            return false;

        vec3v direction = p2 - p1;
        const vec_t distance = (vec_t)math::length(direction);
        if (distance <= 0.0f)
            return false;
        direction = direction * (1.0f / distance);

        if (model.trace_mode == shadow_slow)
        {
            for (const studio_shadow_facet &facet : model.facets)
            {
                if (!bounds_overlap(facet.mins, facet.maxs, bounds_min, bounds_max))
                    continue;
                if (clip_ray_to_facet(model, facet, p1, p2))
                    return true;
            }
            return false;
        }

        for (const studio_shadow_triangle &triangle : model.triangles)
        {
            if (!bounds_overlap(triangle.mins, triangle.maxs, bounds_min, bounds_max))
                continue;
            if (model.trace_mode == shadow_fast)
            {
                if (clip_ray_to_box(p1, p2, triangle.mins, triangle.maxs))
                    return true;
            }
            else
            {
                if (clip_ray_to_triangle(model, triangle, p1, direction, distance))
                    return true;
            }
        }
        return false;
    }

    bool test_segment_against_studio_list(const rad_state &state, const vec3v &p1,
                                          const vec3v &p2)
    {
        if (state.studio_models.empty())
            return false;
        for (const studio_shadow_model &model : state.studio_models)
            if (test_segment_against_studio_model(model, p1, p2))
                return true;
        return false;
    }

    void build_studio_facets(studio_shadow_model &model,
                             const std::vector<vec3v> &positions,
                             const std::vector<std::array<int, 3>> &triangles)
    {
        build_facets(model, positions, triangles);
    }

    bool clip_segment_to_facet(const studio_shadow_model &model,
                               const studio_shadow_facet &facet, const vec3v &start,
                               const vec3v &end)
    {
        return clip_ray_to_facet(model, facet, start, end);
    }
}
