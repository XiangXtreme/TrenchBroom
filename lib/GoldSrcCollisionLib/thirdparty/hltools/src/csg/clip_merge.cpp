#include "clip_merge.h"

#include <map>
#include <set>
#include <utility>

#include "../common/log.h"

namespace csg
{
    namespace
    {
        struct clip_group
        {
            brush_hull hull;
            std::vector<size_t> members;
        };

        bool eligible(const map_brush &source, const built_brush &brush,
                      const brush_build_options &options)
        {
            if (clip_options_for_brush(source, options).clip != clip_type::legacy
                || brush.skipped || brush.contents != content::solid
                || source.noclip || source.cliphull || source.generated_studio
                || brush.hulls[0].faces.empty())
                return false;
            for (const auto &face : brush.hulls[0].faces)
                if (face.bevel) return false;
            for (int h = 1; h < num_hulls; ++h)
                if (brush.hulls[h].faces.empty() || !source.hull_shapes[h].empty()
                    || (options.hull_shapes && !options.hull_shapes->default_hulls[h].disabled))
                    return false;
            return true;
        }

        bool compatible(const built_brush &a, const built_brush &b)
        {
            return a.original_entity_num == b.original_entity_num
                && a.detail_level == b.detail_level && a.chop_down == b.chop_down
                && a.chop_up == b.chop_up && a.clipnode_detail_level == b.clipnode_detail_level
                && a.coplanar_priority == b.coplanar_priority;
        }

        bool supports(const brush_hull &a, const brush_hull &b, int cut,
                      const plane_store &planes)
        {
            for (const auto &face : a.faces)
            {
                if (face.planenum == cut) continue;
                const auto &plane = planes.planes()[(size_t)face.planenum];
                for (const auto &other : b.faces)
                    for (const auto &point : other.winding.points())
                        if (math::dot(plane.normal, point) - plane.dist > (vec_t)1e-7)
                            return false;
            }
            return true;
        }

        bool join(const brush_hull &a, const brush_hull &b, int cut,
                  const map_brush &source, const plane_store &planes,
                  vec_t world_extent, brush_hull &out)
        {
            // Across the shared cut, each remaining plane must contain BOTH
            // solids. A point in their intersection belongs to A or B according
            // to its side of the cut, proving this convex union adds no space.
            if (!supports(a, b, cut, planes) || !supports(b, a, cut ^ 1, planes))
                return false;
            std::map<int, brush_face> boundary;
            for (const auto *hull : {&a, &b})
                for (const auto &face : hull->faces)
                    if (face.planenum != cut && face.planenum != (cut ^ 1))
                        boundary.emplace(face.planenum, face);
            std::vector<brush_face> faces;
            for (const auto &entry : boundary) faces.push_back(entry.second);
            out = make_hull_faces(source, planes, std::move(faces), world_extent);
            return out.faces.size() >= 4;
        }
    }

    void merge_legacy_clip_brushes(const map_source &map, built_entity &entity,
                                  plane_store &planes, const brush_build_options &options)
    {
        if (!options.merge_legacy_clip || options.noclip || options.only_entities) return;
        const auto &source_entity = map.entities[(size_t)entity.entity_num];
        std::vector<clip_group> groups(entity.brushes.size());
        std::map<int, std::set<size_t>> owners;
        std::set<size_t> pending;
        auto source = [&](size_t index) -> const map_brush & {
            return map.brushes[(size_t)source_entity.first_brush + index];
        };
        auto index_faces = [&](size_t i, bool add) {
            for (const auto &face : groups[i].hull.faces)
            {
                if (add) owners[face.planenum].insert(i);
                else owners[face.planenum].erase(i);
            }
        };
        for (size_t i = 0; i < groups.size(); ++i)
            if (eligible(source(i), entity.brushes[i], options))
            {
                groups[i] = {entity.brushes[i].hulls[0], {i}};
                index_faces(i, true);
                pending.insert(i);
            }
        const size_t eligible_count = pending.size();
        size_t merges = 0;
        while (!pending.empty())
        {
            const size_t i = *pending.begin();
            pending.erase(pending.begin());
            bool merged = false;
            for (const auto &face : groups[i].hull.faces)
            {
                const auto neighbors = owners.find(face.planenum ^ 1);
                if (neighbors == owners.end()) continue;
                for (size_t j : neighbors->second)
                {
                    if (j == i || !compatible(entity.brushes[i], entity.brushes[j])) continue;
                    if (groups[i].hull.bounds.disjoint(groups[j].hull.bounds)) continue;
                    brush_hull combined;
                    if (!join(groups[i].hull, groups[j].hull, face.planenum, source(i),
                              planes, options.world_extent, combined)) continue;
                    index_faces(i, false);
                    index_faces(j, false);
                    groups[i].hull = std::move(combined);
                    groups[i].members.insert(groups[i].members.end(),
                                             groups[j].members.begin(), groups[j].members.end());
                    groups[j] = {};
                    index_faces(i, true);
                    pending.erase(j);
                    pending.insert(i);
                    ++merges;
                    merged = true;
                    break;
                }
                if (merged) break;
            }
        }
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (groups[i].members.size() < 2) continue;
            for (int h = 1; h < num_hulls; ++h)
            {
                entity.brushes[i].hulls[h] = make_expanded_clip_hull(
                    source(i), groups[i].hull, planes, h, clip_options_for_brush(source(i), options));
                for (size_t j : groups[i].members)
                    if (j != i) entity.brushes[j].hulls[h] = {};
            }
        }
        if (eligible_count)
            logging::info("clipmerge: entity %d, %zu legacy brushes -> %zu convex collision groups (%zu joins)\n",
                          entity.entity_num, eligible_count, eligible_count - merges, merges);
    }
}
