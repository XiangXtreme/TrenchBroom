#include "studio_simplify.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace rad
{
    namespace
    {
        struct pm_vertex
        {
            math::vec3v position;
            int id = 0;
            std::vector<int> neighbors;
            std::vector<int> faces;
            float cost = 0.0f;
            int collapse = -1;
            bool alive = true;
        };

        struct pm_triangle
        {
            int v[3] = {0, 0, 0};
            math::vec3v normal;
            bool alive = true;
        };

        struct pm_state
        {
            std::vector<pm_vertex> vertices;
            std::vector<pm_triangle> triangles;
        };

        bool contains(const std::vector<int> &list, int value)
        {
            return std::find(list.begin(), list.end(), value) != list.end();
        }

        void remove_value(std::vector<int> &list, int value)
        {
            list.erase(std::remove(list.begin(), list.end(), value), list.end());
        }

        void add_unique(std::vector<int> &list, int value)
        {
            if (!contains(list, value))
                list.push_back(value);
        }

        bool triangle_has_vertex(const pm_triangle &triangle, int vertex)
        {
            return triangle.v[0] == vertex || triangle.v[1] == vertex
                || triangle.v[2] == vertex;
        }

        // mirrors the reference VectorLength macro: float products accumulated
        // in double, one double sqrt
        float vector_length(const math::vec3v &v)
        {
            return (float)std::sqrt((double)((double)(v.x * v.x) + (double)(v.y * v.y)
                                              + (double)(v.z * v.z)));
        }

        // mirrors VectorNormalize: float dot, double sqrt, double division
        void normalize_vector(math::vec3v &v)
        {
            const double length = std::sqrt((double)math::dot(v, v));
            if (length < 1e-5)
            {
                v = math::vec3v{};
                return;
            }
            v = math::vec3v{(vec_t)(v.x / length), (vec_t)(v.y / length),
                            (vec_t)(v.z / length)};
        }

        void compute_normal(pm_triangle &triangle, const std::vector<pm_vertex> &vertices)
        {
            const math::vec3v a = vertices[(std::size_t)triangle.v[1]].position
                - vertices[(std::size_t)triangle.v[0]].position;
            const math::vec3v b = vertices[(std::size_t)triangle.v[2]].position
                - vertices[(std::size_t)triangle.v[1]].position;
            math::vec3v normal = math::cross(a, b);
            normalize_vector(normal);
            triangle.normal = normal;
        }

        void remove_if_non_neighbor(pm_state &state, int u, int n)
        {
            pm_vertex &vertex = state.vertices[(std::size_t)u];
            if (!contains(vertex.neighbors, n))
                return;
            for (int face : vertex.faces)
            {
                if (triangle_has_vertex(state.triangles[(std::size_t)face], n))
                    return;
            }
            remove_value(vertex.neighbors, n);
        }

        void remove_triangle(pm_state &state, int index)
        {
            pm_triangle &triangle = state.triangles[(std::size_t)index];
            if (!triangle.alive)
                return;
            triangle.alive = false;
            for (int k = 0; k < 3; k++)
                remove_value(state.vertices[(std::size_t)triangle.v[k]].faces, index);
            for (int k = 0; k < 3; k++)
            {
                const int a = triangle.v[k];
                const int b = triangle.v[(k + 1) % 3];
                remove_if_non_neighbor(state, a, b);
                remove_if_non_neighbor(state, b, a);
            }
        }

        void replace_vertex(pm_state &state, int triangle_index, int old_vertex,
                            int new_vertex)
        {
            pm_triangle &triangle = state.triangles[(std::size_t)triangle_index];
            for (int k = 0; k < 3; k++)
            {
                if (triangle.v[k] == old_vertex)
                {
                    triangle.v[k] = new_vertex;
                    break;
                }
            }

            remove_value(state.vertices[(std::size_t)old_vertex].faces, triangle_index);
            add_unique(state.vertices[(std::size_t)new_vertex].faces, triangle_index);

            for (int k = 0; k < 3; k++)
            {
                remove_if_non_neighbor(state, old_vertex, triangle.v[k]);
                remove_if_non_neighbor(state, triangle.v[k], old_vertex);
            }
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    if (i != j)
                        add_unique(state.vertices[(std::size_t)triangle.v[i]].neighbors,
                                   triangle.v[j]);
                }
            }
            compute_normal(triangle, state.vertices);
        }

        float edge_collapse_cost(const pm_state &state, int u, int v)
        {
            const math::vec3v edge = state.vertices[(std::size_t)v].position
                - state.vertices[(std::size_t)u].position;
            const float edge_length = vector_length(edge);
            float curvature = 0.0f;

            std::vector<int> sides;
            for (int face : state.vertices[(std::size_t)u].faces)
            {
                if (triangle_has_vertex(state.triangles[(std::size_t)face], v))
                    sides.push_back(face);
            }

            for (int face_a : state.vertices[(std::size_t)u].faces)
            {
                const math::vec3v &normal_a =
                    state.triangles[(std::size_t)face_a].normal;
                float min_curv = 1.0f;
                for (int face_b : sides)
                {
                    const math::vec3v &normal_b =
                        state.triangles[(std::size_t)face_b].normal;
                    const float dot = math::dot(normal_a, normal_b);
                    min_curv = std::min(min_curv, (1.0f - dot) * 0.5f);
                }
                curvature = std::max(curvature, min_curv);
            }
            return edge_length * curvature;
        }

        void compute_edge_cost(pm_state &state, int index)
        {
            pm_vertex &vertex = state.vertices[(std::size_t)index];
            if (vertex.neighbors.empty())
            {
                vertex.collapse = -1;
                vertex.cost = -0.01f;
                return;
            }
            vertex.cost = 1000000.0f;
            vertex.collapse = -1;
            for (int neighbor : vertex.neighbors)
            {
                const float cost = edge_collapse_cost(state, index, neighbor);
                if (cost < vertex.cost)
                {
                    vertex.cost = cost;
                    vertex.collapse = neighbor;
                }
            }
        }

        void collapse(pm_state &state, int u, int v)
        {
            pm_vertex &vertex = state.vertices[(std::size_t)u];
            if (v < 0)
            {
                for (int neighbor : vertex.neighbors)
                    remove_value(state.vertices[(std::size_t)neighbor].neighbors, u);
                vertex.neighbors.clear();
                vertex.alive = false;
                return;
            }

            const std::vector<int> neighbors = vertex.neighbors;

            for (int i = (int)vertex.faces.size() - 1; i >= 0; i--)
            {
                const int face = vertex.faces[(std::size_t)i];
                if (triangle_has_vertex(state.triangles[(std::size_t)face], v))
                    remove_triangle(state, face);
            }
            for (int i = (int)vertex.faces.size() - 1; i >= 0; i--)
                replace_vertex(state, vertex.faces[(std::size_t)i], u, v);

            for (int neighbor : vertex.neighbors)
                remove_value(state.vertices[(std::size_t)neighbor].neighbors, u);
            vertex.neighbors.clear();
            vertex.faces.clear();
            vertex.alive = false;

            for (int neighbor : neighbors)
            {
                if (state.vertices[(std::size_t)neighbor].alive)
                    compute_edge_cost(state, neighbor);
            }
        }

        int minimum_cost_vertex(const pm_state &state)
        {
            int best = -1;
            for (int i = 0; i < (int)state.vertices.size(); i++)
            {
                const pm_vertex &vertex = state.vertices[(std::size_t)i];
                if (!vertex.alive)
                    continue;
                if (best < 0 || vertex.cost < state.vertices[(std::size_t)best].cost)
                    best = i;
            }
            return best;
        }
    }

    int simplified_vertex_target(int source_vertices)
    {
        if (source_vertices <= 400)
            return source_vertices;
        if (source_vertices <= 600)
            return (int)((double)source_vertices * 0.85);
        if (source_vertices <= 1500)
            return (int)((double)source_vertices * 0.55);
        return (int)((double)source_vertices * 0.15);
    }

    simplified_mesh simplify_shadow_mesh(const std::vector<math::vec3v> &positions,
                                         const std::vector<std::array<int, 3>> &triangles)
    {
        simplified_mesh out;
        out.original_positions = positions;
        out.source_vertices = (int)positions.size();
        if (positions.empty() || triangles.empty())
            return out;

        pm_state state;
        state.vertices.resize(positions.size());
        for (int i = 0; i < (int)positions.size(); i++)
        {
            state.vertices[(std::size_t)i].position = positions[(std::size_t)i];
            state.vertices[(std::size_t)i].id = i;
        }
        for (const std::array<int, 3> &indices : triangles)
        {
            if (indices[0] < 0 || indices[1] < 0 || indices[2] < 0
                || (std::size_t)indices[0] >= positions.size()
                || (std::size_t)indices[1] >= positions.size()
                || (std::size_t)indices[2] >= positions.size())
                continue;
            if (indices[0] == indices[1] || indices[1] == indices[2]
                || indices[2] == indices[0])
                continue;

            pm_triangle triangle;
            std::copy(indices.begin(), indices.end(), triangle.v);
            const int index = (int)state.triangles.size();
            state.triangles.push_back(triangle);
            for (int k = 0; k < 3; k++)
                state.vertices[(std::size_t)triangle.v[k]].faces.push_back(index);
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    if (i != j)
                        add_unique(state.vertices[(std::size_t)triangle.v[i]].neighbors,
                                   triangle.v[j]);
                }
            }
            compute_normal(state.triangles.back(), state.vertices);
        }
        if (state.triangles.empty())
            return out;

        for (int i = 0; i < (int)state.vertices.size(); i++)
            compute_edge_cost(state, i);

        const int count = (int)state.vertices.size();
        std::vector<int> permutation((std::size_t)count, 0);
        std::vector<int> map((std::size_t)count, 0);
        int live = count;
        while (live > 0)
        {
            const int mn = minimum_cost_vertex(state);
            if (mn < 0)
                break;
            permutation[(std::size_t)state.vertices[(std::size_t)mn].id] = live - 1;
            const int collapse_to = state.vertices[(std::size_t)mn].collapse;
            map[(std::size_t)(live - 1)] = collapse_to >= 0
                ? state.vertices[(std::size_t)collapse_to].id
                : -1;
            collapse(state, mn, collapse_to);
            live--;
        }

        for (int &entry : map)
            entry = entry == -1 ? 0 : permutation[(std::size_t)entry];

        std::vector<math::vec3v> permuted(positions.size());
        for (int i = 0; i < count; i++)
            permuted[(std::size_t)permutation[(std::size_t)i]] = positions[(std::size_t)i];

        const int target = std::max(1, simplified_vertex_target(count));
        auto map_vertex = [&](int index) {
            while (index >= target)
                index = map[(std::size_t)index];
            return index;
        };

        out.positions.reserve((std::size_t)target);
        for (int i = 0; i < target; i++)
            out.positions.push_back(permuted[(std::size_t)i]);

        for (const std::array<int, 3> &indices : triangles)
        {
            if (indices[0] < 0 || indices[1] < 0 || indices[2] < 0
                || (std::size_t)indices[0] >= positions.size()
                || (std::size_t)indices[1] >= positions.size()
                || (std::size_t)indices[2] >= positions.size())
                continue;
            const int a = map_vertex(permutation[(std::size_t)indices[0]]);
            const int b = map_vertex(permutation[(std::size_t)indices[1]]);
            const int c = map_vertex(permutation[(std::size_t)indices[2]]);
            if (a == b || b == c || c == a)
                continue;
            out.triangles.push_back({a, b, c});
        }
        return out;
    }
}
