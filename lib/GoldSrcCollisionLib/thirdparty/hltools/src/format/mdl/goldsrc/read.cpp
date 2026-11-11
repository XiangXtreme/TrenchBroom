#include "read.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/binary.h"
#include "model.h"

namespace format
{
    namespace
    {
        // on disk record sizes for studio version 10. these repeat the private
        // constants in model.cpp on purpose: the reader validates the same
        // layout, and both sides of the format are allowed to evolve apart.
        constexpr std::size_t header_size = 244;
        constexpr std::size_t bone_size = 112;
        constexpr std::size_t seqdesc_size = 176;
        constexpr std::size_t bodypart_size = 76;
        constexpr std::size_t model_size = 112;
        constexpr std::size_t mesh_size = 20;
        constexpr std::size_t texture_size = 80;
        constexpr std::size_t anim_size = 12;
        constexpr std::size_t bone_name_size = 32;
        constexpr std::size_t texture_name_size = 64;
        constexpr std::size_t palette_entries = 256;
        constexpr std::size_t palette_bytes = palette_entries * 3;

        // engine limits from studio.h
        constexpr int max_bones = 128;
        constexpr int max_textures = 256;
        constexpr int max_bodyparts = 32;
        constexpr int max_texture_size = 4096;
        constexpr int max_vertices = 1 << 21;
        constexpr int max_command_vertices = 1 << 16;
        constexpr int max_command_runs = 1 << 16;

        // texture flags the reader cares about
        constexpr unsigned uv_coords_flag = 1u << 31; // STUDIO_NF_UV_COORDS

        struct view
        {
            const byte *data = nullptr;
            std::size_t size = 0;

            bool has(std::size_t at, std::size_t count) const
            {
                return at <= size && count <= size - at;
            }

            bool i16(std::size_t at, std::int16_t &value) const
            {
                binary::reader reader(data, size);
                return has(at, 2) && reader.i16_at(at, value);
            }

            bool u16(std::size_t at, std::uint16_t &value) const
            {
                binary::reader reader(data, size);
                return has(at, 2) && reader.u16_at(at, value);
            }

            bool i32(std::size_t at, std::int32_t &value) const
            {
                binary::reader reader(data, size);
                return has(at, 4) && reader.i32_at(at, value);
            }

            bool f32(std::size_t at, float &value) const
            {
                binary::reader reader(data, size);
                return has(at, 4) && reader.f32_at(at, value);
            }

            std::string name(std::size_t at, std::size_t capacity) const
            {
                std::string out;
                for (std::size_t i = 0; i < capacity && at + i < size; i++)
                {
                    const char c = (char)data[at + i];
                    if (c == '\0')
                        break;
                    out.push_back(c);
                }
                return out;
            }
        };

        bool fail(std::string *error, const std::string &message)
        {
            if (error)
                *error = message;
            return false;
        }

        // a row basis matrix with the translation in the last column, matching
        // the layout goldsrc's ConcatTransforms and VectorTransform expect
        struct transform
        {
            float m[3][4];
        };

        // the reference AngleQuaternion + QuaternionMatrix pair, kept in double
        // precision here because a chain of parent transforms multiplies the
        // error of the float quaternion helper the engine uses
        transform bone_transform(const float angles[3], const float position[3])
        {
            const double ax = (double)angles[0] * 0.5;
            const double ay = (double)angles[1] * 0.5;
            const double az = (double)angles[2] * 0.5;
            const double sr = std::sin(ax), cr = std::cos(ax);
            const double sp = std::sin(ay), cp = std::cos(ay);
            const double sy = std::sin(az), cy = std::cos(az);
            const double x = sr * cp * cy + cr * sp * sy;
            const double y = cr * sp * cy + sr * cp * sy;
            const double z = cr * cp * sy - sr * sp * cy;
            const double w = cr * cp * cy - sr * sp * sy;

            transform out{};
            out.m[0][0] = (float)(1.0 - 2.0 * y * y - 2.0 * z * z);
            out.m[1][0] = (float)(2.0 * x * y + 2.0 * w * z);
            out.m[2][0] = (float)(2.0 * x * z - 2.0 * w * y);
            out.m[0][1] = (float)(2.0 * x * y - 2.0 * w * z);
            out.m[1][1] = (float)(1.0 - 2.0 * x * x - 2.0 * z * z);
            out.m[2][1] = (float)(2.0 * y * z + 2.0 * w * x);
            out.m[0][2] = (float)(2.0 * x * z + 2.0 * w * y);
            out.m[1][2] = (float)(2.0 * y * z - 2.0 * w * x);
            out.m[2][2] = (float)(1.0 - 2.0 * x * x - 2.0 * y * y);
            out.m[0][3] = position[0];
            out.m[1][3] = position[1];
            out.m[2][3] = position[2];
            return out;
        }

        transform concat(const transform &parent, const transform &local)
        {
            transform out{};
            for (int row = 0; row < 3; row++)
            {
                for (int column = 0; column < 3; column++)
                    out.m[row][column] = parent.m[row][0] * local.m[0][column]
                        + parent.m[row][1] * local.m[1][column]
                        + parent.m[row][2] * local.m[2][column];
                out.m[row][3] = parent.m[row][0] * local.m[0][3]
                    + parent.m[row][1] * local.m[1][3]
                    + parent.m[row][2] * local.m[2][3] + parent.m[row][3];
            }
            return out;
        }

        void transform_point(const transform &t, const float in[3], float out[3])
        {
            for (int row = 0; row < 3; row++)
                out[row] = t.m[row][0] * in[0] + t.m[row][1] * in[1]
                    + t.m[row][2] * in[2] + t.m[row][3];
        }

        void transform_vector(const transform &t, const float in[3], float out[3])
        {
            for (int row = 0; row < 3; row++)
                out[row] = t.m[row][0] * in[0] + t.m[row][1] * in[1]
                    + t.m[row][2] * in[2];
        }

        // goldsrc packs a compressed per bone delta as a run of values: a
        // (valid,total) byte pair followed by `valid` shorts, with the rest of
        // the run repeating the last one
        bool decode_channel(const view &f, std::size_t at, int frame, float &value)
        {
            int remaining = frame;
            for (int guard = 0; guard < 4096; guard++)
            {
                if (!f.has(at, 2))
                    return false;
                const int valid = f.data[at];
                const int total = f.data[at + 1];
                if (valid <= 0 || total < valid)
                    return false;
                if (remaining < total)
                {
                    const int index = remaining < valid ? remaining : valid - 1;
                    std::int16_t raw = 0;
                    if (!f.i16(at + 2 + (std::size_t)index * 2, raw))
                        return false;
                    value = (float)raw;
                    return true;
                }
                remaining -= total;
                at += 2 + (std::size_t)valid * 2;
            }
            return false;
        }

        float half_to_float(std::uint16_t h)
        {
            const std::uint32_t sign = (std::uint32_t)(h & 0x8000u) << 16;
            std::uint32_t exponent = (h >> 10) & 0x1Fu;
            std::uint32_t mantissa = h & 0x3FFu;
            std::uint32_t bits;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    exponent = 127 - 15 + 1;
                    while ((mantissa & 0x400u) == 0)
                    {
                        mantissa <<= 1;
                        exponent--;
                    }
                    mantissa &= 0x3FFu;
                    bits = sign | (exponent << 23) | (mantissa << 13);
                }
            }
            else if (exponent == 31)
            {
                bits = sign | 0x7F800000u | (mantissa << 13);
            }
            else
            {
                bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
            }
            float out;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }

        // goldsrc stores the three smaller sampled mip levels between the base
        // pixels and the palette
        std::size_t mip_bytes(unsigned width, unsigned height)
        {
            return (std::size_t)(width >> 1) * (height >> 1)
                + (std::size_t)(width >> 2) * (height >> 2)
                + (std::size_t)(width >> 3) * (height >> 3);
        }

        // StudioMDL writes base pixels, mips and palette; the hltools writer
        // emits only base pixels and palette. decide by where the next texture
        // header starts, or by the end of the file for the last texture.
        std::size_t palette_offset(const view &f, std::size_t index, unsigned width,
                                   unsigned height, std::size_t next_index,
                                   bool has_next)
        {
            const std::size_t base = (std::size_t)width * height;
            const std::size_t mips = mip_bytes(width, height);
            if (has_next)
            {
                if (index + base + palette_bytes == next_index)
                    return index + base;
                if (index + base + mips + palette_bytes == next_index)
                    return index + base + mips;
            }
            else
            {
                if (index + base + palette_bytes == f.size)
                    return index + base;
                if (f.has(index + base + mips, palette_bytes))
                    return index + base + mips;
                if (f.has(index + base, palette_bytes))
                    return index + base;
            }
            return index + base + mips;
        }

        struct bone_record
        {
            std::string name;
            int parent = -1;
            float value[6] = {};
            float scale[6] = {};
        };
    }

    bool read_goldsrc_model(const byte *data, std::size_t size,
                            const goldsrc_model_pose &pose, studio_model &out,
                            std::string *error)
    {
        out = studio_model();

        if (!data || size < header_size)
            return fail(error, "studio model is shorter than a version 10 header");
        if (std::memcmp(data, "IDST", 4) != 0)
            return fail(error, "studio model does not start with IDST");
        const view f{data, size};

        std::int32_t version = 0;
        if (!f.i32(4, version))
            return fail(error, "studio model header is truncated");
        if (version != goldsrc_studio_version)
            return fail(error, "studio model version " + std::to_string(version)
                                   + " is not the supported version "
                                   + std::to_string(goldsrc_studio_version));

        std::int32_t length = 0;
        if (!f.i32(72, length))
            return fail(error, "studio model header is truncated");
        if (length < (std::int32_t)header_size || (std::size_t)length > size)
            return fail(error, "studio model length field does not fit the file");

        out.name = f.name(8, 64);

        std::int32_t numbones = 0, boneindex = 0;
        std::int32_t numseq = 0, seqindex = 0;
        std::int32_t numtextures = 0, textureindex = 0;
        std::int32_t numskinref = 0, numskinfamilies = 0, skinindex = 0;
        std::int32_t numbodyparts = 0, bodypartindex = 0;
        if (!f.i32(140, numbones) || !f.i32(144, boneindex)
            || !f.i32(164, numseq) || !f.i32(168, seqindex)
            || !f.i32(180, numtextures) || !f.i32(184, textureindex)
            || !f.i32(192, numskinref) || !f.i32(196, numskinfamilies)
            || !f.i32(200, skinindex)
            || !f.i32(204, numbodyparts) || !f.i32(208, bodypartindex))
            return fail(error, "studio model tables are truncated");

        if (numbones < 1 || numbones > max_bones)
            return fail(error, "studio model bone count is out of range");
        if (numtextures < 0 || numtextures > max_textures)
            return fail(error, "studio model texture count is out of range");
        if (numbodyparts < 0 || numbodyparts > max_bodyparts)
            return fail(error, "studio model bodypart count is out of range");
        if (numskinref < 0 || numskinfamilies < 0)
            return fail(error, "studio model skin tables are out of range");
        if (!f.has(boneindex, (std::size_t)numbones * bone_size))
            return fail(error, "studio model bone table is truncated");
        if (numseq < 0 || !f.has(seqindex, (std::size_t)numseq * seqdesc_size))
            return fail(error, "studio model sequence table is truncated");
        if (!f.has(textureindex, (std::size_t)numtextures * texture_size))
            return fail(error, "studio model texture table is truncated");
        if (numskinref > 0 && numskinfamilies > 0
            && !f.has(skinindex, (std::size_t)numskinref * numskinfamilies * 2))
            return fail(error, "studio model skin table is truncated");
        if (!f.has(bodypartindex, (std::size_t)numbodyparts * bodypart_size))
            return fail(error, "studio model bodypart table is truncated");

        // ===== bones =====
        std::vector<bone_record> bones((std::size_t)numbones);
        for (int b = 0; b < numbones; b++)
        {
            const std::size_t at = (std::size_t)boneindex + (std::size_t)b * bone_size;
            bone_record &bone = bones[(std::size_t)b];
            bone.name = f.name(at, bone_name_size);
            std::int32_t parent = 0;
            if (!f.i32(at + 32, parent))
                return fail(error, "studio model bone table is truncated");
            bone.parent = parent;
            for (int d = 0; d < 6; d++)
                if (!f.f32(at + 64 + (std::size_t)d * 4, bone.value[d])
                    || !f.f32(at + 88 + (std::size_t)d * 4, bone.scale[d]))
                    return fail(error, "studio model bone table is truncated");
            if (bone.parent < -1 || bone.parent >= b)
                return fail(error, "studio model bone " + std::to_string(b)
                                       + " has an invalid parent");

            studio_bone stored;
            stored.name = bone.name;
            stored.parent = bone.parent;
            for (int d = 0; d < 3; d++)
            {
                stored.position[d] = bone.value[d];
                stored.rotation[d] = bone.value[d + 3];
            }
            out.bones.push_back(std::move(stored));
        }

        // ===== requested sequence frame =====
        std::vector<float> position((std::size_t)numbones * 3);
        std::vector<float> angles((std::size_t)numbones * 3);
        for (int b = 0; b < numbones; b++)
        {
            for (int d = 0; d < 3; d++)
            {
                position[(std::size_t)b * 3 + (std::size_t)d] = bones[(std::size_t)b].value[d];
                angles[(std::size_t)b * 3 + (std::size_t)d] = bones[(std::size_t)b].value[d + 3];
            }
        }

        if (numseq > 0)
        {
            if (pose.sequence < 0 || pose.sequence >= numseq)
            {
                return fail(error, "studio model sequence " + std::to_string(pose.sequence)
                                       + " is out of range");
            }
            const std::size_t seq_at = (std::size_t)seqindex
                + (std::size_t)pose.sequence * seqdesc_size;
            std::int32_t numframes = 0, numblends = 0, animindex = 0;
            if (!f.i32(seq_at + 56, numframes) || !f.i32(seq_at + 120, numblends)
                || !f.i32(seq_at + 124, animindex))
                return fail(error, "studio model sequence table is truncated");
            if (numframes > 0 && animindex > 0)
            {
                const int frame = std::max(0, std::min(pose.frame, numframes - 1));
                const int blends = std::max(1, numblends);
                if (!f.has((std::size_t)animindex,
                           (std::size_t)numbones * anim_size * (std::size_t)blends))
                    return fail(error, "studio model animation table is truncated");
                for (int b = 0; b < numbones; b++)
                {
                    const std::size_t anim = (std::size_t)animindex
                        + (std::size_t)b * anim_size * (std::size_t)blends;
                    for (int d = 0; d < 6; d++)
                    {
                        std::uint16_t offset = 0;
                        if (!f.u16(anim + (std::size_t)d * 2, offset))
                            return fail(error, "studio model animation table is truncated");
                        if (offset == 0)
                            continue;
                        float delta = 0;
                        if (!decode_channel(f, anim + offset, frame, delta))
                            return fail(error, "studio model animation channel is malformed");
                        const float base = bones[(std::size_t)b].value[d];
                        const float scale = bones[(std::size_t)b].scale[d];
                        const float value = base + delta * scale;
                        if (d < 3)
                            position[(std::size_t)b * 3 + (std::size_t)d] = value;
                        else
                            angles[(std::size_t)b * 3 + (std::size_t)(d - 3)] = value;
                    }
                }
            }
        }

        std::vector<transform> world((std::size_t)numbones);
        for (int b = 0; b < numbones; b++)
        {
            float local_angles[3] = {angles[(std::size_t)b * 3],
                                     angles[(std::size_t)b * 3 + 1],
                                     angles[(std::size_t)b * 3 + 2]};
            float local_position[3] = {position[(std::size_t)b * 3],
                                       position[(std::size_t)b * 3 + 1],
                                       position[(std::size_t)b * 3 + 2]};
            const transform local = bone_transform(local_angles, local_position);
            const int parent = bones[(std::size_t)b].parent;
            world[(std::size_t)b] = parent < 0
                ? local
                : concat(world[(std::size_t)parent], local);
        }

        // ===== textures =====
        for (int t = 0; t < numtextures; t++)
        {
            const std::size_t at = (std::size_t)textureindex + (std::size_t)t * texture_size;
            std::int32_t flags = 0, width = 0, height = 0, index = 0;
            if (!f.i32(at + 64, flags) || !f.i32(at + 68, width) || !f.i32(at + 72, height)
                || !f.i32(at + 76, index))
                return fail(error, "studio model texture table is truncated");
            if (width <= 0 || height <= 0 || width > max_texture_size
                || height > max_texture_size)
                return fail(error, "studio model texture " + std::to_string(t)
                                       + " has an invalid size");
            const std::size_t pixels = (std::size_t)width * (std::size_t)height;
            if (index < 0 || !f.has((std::size_t)index, pixels))
                return fail(error, "studio model texture " + std::to_string(t)
                                       + " pixel data is truncated");

            studio_texture texture;
            texture.name = f.name(at, texture_name_size);
            texture.flags = flags;
            texture.image.width = (unsigned)width;
            texture.image.height = (unsigned)height;
            texture.image.pixels.assign(data + index, data + index + pixels);

            std::size_t next_index = 0;
            bool has_next = false;
            if (t + 1 < numtextures)
            {
                std::int32_t raw = 0;
                if (!f.i32((std::size_t)textureindex + (std::size_t)(t + 1) * texture_size + 76,
                           raw))
                    return fail(error, "studio model texture table is truncated");
                if (raw > 0)
                {
                    next_index = (std::size_t)raw;
                    has_next = true;
                }
            }
            const std::size_t palette = palette_offset(f, (std::size_t)index,
                                                       (unsigned)width, (unsigned)height,
                                                       next_index, has_next);
            if (f.has(palette, palette_bytes))
                for (std::size_t i = 0; i < palette_entries; i++)
                    for (std::size_t k = 0; k < 3; k++)
                        texture.image.palette[i][k] = data[palette + i * 3 + k];

            out.materials.push_back(texture.name);
            out.textures.push_back(std::move(texture));
        }

        // ===== skin family =====
        int skin_family = 0;
        if (pose.skin > 0 && pose.skin < numskinfamilies)
            skin_family = pose.skin;
        std::vector<int> skinrefs((std::size_t)numskinref, 0);
        for (int s = 0; s < numskinref; s++)
        {
            std::int16_t raw = 0;
            const std::size_t at = (std::size_t)skinindex
                + ((std::size_t)skin_family * (std::size_t)numskinref + (std::size_t)s) * 2;
            if (!f.i16(at, raw))
                return fail(error, "studio model skin table is truncated");
            skinrefs[(std::size_t)s] = raw;
        }

        // ===== geometry =====
        for (int part = 0; part < numbodyparts; part++)
        {
            const std::size_t part_at = (std::size_t)bodypartindex
                + (std::size_t)part * bodypart_size;
            std::int32_t nummodels = 0, base = 0, modelindex = 0;
            if (!f.i32(part_at + 64, nummodels) || !f.i32(part_at + 68, base)
                || !f.i32(part_at + 72, modelindex))
                return fail(error, "studio model bodypart table is truncated");
            if (nummodels <= 0)
                continue;

            // goldsrc distributes the entity's body value across bodyparts
            const int divisor = base > 0 ? base : 1;
            const int selected = ((pose.body / divisor) % nummodels + nummodels) % nummodels;
            const std::size_t model_at = (std::size_t)modelindex
                + (std::size_t)selected * model_size;
            if (!f.has(model_at, model_size))
                return fail(error, "studio model bodypart model table is truncated");

            std::int32_t nummesh = 0, meshindex = 0, numverts = 0, vertinfoindex = 0,
                         vertindex = 0, numnorms = 0, norminfoindex = 0, normindex = 0;
            if (!f.i32(model_at + 72, nummesh) || !f.i32(model_at + 76, meshindex)
                || !f.i32(model_at + 80, numverts) || !f.i32(model_at + 84, vertinfoindex)
                || !f.i32(model_at + 88, vertindex) || !f.i32(model_at + 92, numnorms)
                || !f.i32(model_at + 96, norminfoindex) || !f.i32(model_at + 100, normindex))
                return fail(error, "studio model bodypart model is truncated");
            if (numverts <= 0 || nummesh < 0 || !f.has(vertindex, (std::size_t)numverts * 12)
                || !f.has(vertinfoindex, (std::size_t)numverts))
                return fail(error, "studio model bodypart vertex table is truncated");
            if (numnorms > 0
                && (!f.has(normindex, (std::size_t)numnorms * 12)
                    || !f.has(norminfoindex, (std::size_t)numnorms)))
                return fail(error, "studio model bodypart normal table is truncated");
            if (!f.has(meshindex, (std::size_t)nummesh * mesh_size))
                return fail(error, "studio model bodypart mesh table is truncated");

            // pose the position and normal tables once, then expand triangle
            // commands into independent vertices so a UV seam keeps both sides
            std::vector<studio_vertex> position_table((std::size_t)numverts);
            for (int v = 0; v < numverts; v++)
            {
                float in[3] = {};
                for (int k = 0; k < 3; k++)
                    if (!f.f32(vertindex + (std::size_t)v * 12 + (std::size_t)k * 4, in[k]))
                        return fail(error, "studio model bodypart vertex table is truncated");
                const byte bone = data[vertinfoindex + (std::size_t)v];
                if (bone >= numbones)
                    return fail(error, "studio model vertex references an invalid bone");
                studio_vertex &vertex = position_table[(std::size_t)v];
                transform_point(world[bone], in, vertex.position);
                vertex.bone = bone;
            }

            std::vector<float> normal_table((std::size_t)std::max(0, numnorms) * 3, 0.0f);
            for (int n = 0; n < numnorms; n++)
            {
                float raw[3] = {};
                for (int k = 0; k < 3; k++)
                    if (!f.f32(normindex + (std::size_t)n * 12 + (std::size_t)k * 4, raw[k]))
                        return fail(error, "studio model normal table is truncated");
                const byte bone = data[norminfoindex + (std::size_t)n];
                if (bone >= numbones)
                    return fail(error, "studio model normal references an invalid bone");
                float out_normal[3];
                transform_vector(world[bone], raw, out_normal);
                for (int k = 0; k < 3; k++)
                    normal_table[(std::size_t)n * 3 + (std::size_t)k] = out_normal[k];
            }

            for (int m = 0; m < nummesh; m++)
            {
                const std::size_t mesh_at = (std::size_t)meshindex
                    + (std::size_t)m * mesh_size;
                std::int32_t numtris = 0, triindex = 0, skinref = 0;
                if (!f.i32(mesh_at + 0, numtris) || !f.i32(mesh_at + 4, triindex)
                    || !f.i32(mesh_at + 8, skinref))
                    return fail(error, "studio model mesh table is truncated");
                (void)numtris;
                // models whose skins live in a sibling T.mdl carry no textures
                // here; geometry is still complete, so the mesh keeps material
                // -1 and the caller treats the surface as opaque
                int texture_index = -1;
                float inv_width = 0.0f;
                float inv_height = 0.0f;
                bool uv_coords = false;
                if (!out.textures.empty())
                {
                    if (skinref < 0 || (std::size_t)skinref >= skinrefs.size())
                        return fail(error, "studio model mesh references an invalid skin slot");
                    texture_index = skinrefs[(std::size_t)skinref];
                    if (texture_index < 0 || texture_index >= (int)out.textures.size())
                        return fail(error,
                                    "studio model skin slot references an invalid texture");

                    const studio_texture &texture = out.textures[(std::size_t)texture_index];
                    inv_width = texture.image.width ? 1.0f / (float)texture.image.width : 0.0f;
                    inv_height = texture.image.height ? 1.0f / (float)texture.image.height : 0.0f;
                    uv_coords = ((unsigned)texture.flags & uv_coords_flag) != 0;
                }

                studio_mesh mesh;
                mesh.material = texture_index;
                std::size_t at = (std::size_t)triindex;
                for (int run = 0; run < max_command_runs; run++)
                {
                    std::int16_t raw_count = 0;
                    if (!f.i16(at, raw_count))
                        return fail(error, "studio model triangle commands are truncated");
                    at += 2;
                    if (raw_count == 0)
                        break;
                    const bool strip = raw_count > 0;
                    const int count = raw_count < 0 ? -raw_count : raw_count;
                    if (count > max_command_vertices || !f.has(at, (std::size_t)count * 8))
                        return fail(error, "studio model triangle command is malformed");

                    int vertex_state = 0;
                    for (int k = 0; k < count; k++, at += 8)
                    {
                        std::int16_t words[4] = {};
                        for (int w = 0; w < 4; w++)
                            if (!f.i16(at + (std::size_t)w * 2, words[w]))
                                return fail(error, "studio model triangle command is truncated");
                        const int pindex = words[0];
                        const int nindex = words[1];
                        if (pindex < 0 || pindex >= numverts)
                            return fail(error, "studio model triangle references an invalid vertex");
                        if (nindex < 0 || (numnorms > 0 && nindex >= numnorms))
                            return fail(error, "studio model triangle references an invalid normal");

                        studio_vertex vertex = position_table[(std::size_t)pindex];
                        if (numnorms > 0)
                        {
                            vertex.normal[0] = normal_table[(std::size_t)nindex * 3];
                            vertex.normal[1] = normal_table[(std::size_t)nindex * 3 + 1];
                            vertex.normal[2] = normal_table[(std::size_t)nindex * 3 + 2];
                        }
                        vertex.u = uv_coords ? half_to_float((std::uint16_t)words[2])
                                             : (float)words[2] * inv_width;
                        vertex.v = uv_coords ? half_to_float((std::uint16_t)words[3])
                                             : (float)words[3] * inv_height;

                        if ((int)out.vertices.size() >= max_vertices)
                            return fail(error, "studio model exceeds the vertex limit");
                        out.vertices.push_back(vertex);
                        const int current = (int)out.vertices.size() - 1;

                        if (vertex_state++ < 3)
                        {
                            mesh.indices.push_back(current);
                        }
                        else if (strip)
                        {
                            if (vertex_state & 1)
                            {
                                mesh.indices.push_back(current - 2);
                                mesh.indices.push_back(current - 1);
                                mesh.indices.push_back(current);
                            }
                            else
                            {
                                mesh.indices.push_back(current - 1);
                                mesh.indices.push_back(current - 2);
                                mesh.indices.push_back(current);
                            }
                        }
                        else
                        {
                            mesh.indices.push_back(current - (vertex_state - 1));
                            mesh.indices.push_back(current - 1);
                            mesh.indices.push_back(current);
                        }
                    }
                }
                out.meshes.push_back(std::move(mesh));
            }
        }

        // the file's bounds describe the bind pose; recompute them from the
        // frame the caller asked for so stale values do not leak out
        out.bbmin[0] = out.bbmin[1] = out.bbmin[2] = 1e30f;
        out.bbmax[0] = out.bbmax[1] = out.bbmax[2] = -1e30f;
        for (const studio_vertex &vertex : out.vertices)
            for (int k = 0; k < 3; k++)
            {
                out.bbmin[k] = std::min(out.bbmin[k], vertex.position[k]);
                out.bbmax[k] = std::max(out.bbmax[k], vertex.position[k]);
            }
        if (out.vertices.empty())
        {
            out.bbmin[0] = out.bbmin[1] = out.bbmin[2] = 0;
            out.bbmax[0] = out.bbmax[1] = out.bbmax[2] = 0;
        }

        return true;
    }

    bool read_goldsrc_model(const std::vector<byte> &data,
                            const goldsrc_model_pose &pose, studio_model &out,
                            std::string *error)
    {
        return read_goldsrc_model(data.data(), data.size(), pose, out, error);
    }
}
