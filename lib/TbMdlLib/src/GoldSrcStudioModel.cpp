/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "mdl/GoldSrcStudioModel.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>

namespace tb::mdl
{
namespace
{

using Bytes = std::vector<std::uint8_t>;

// GoldSrc studio format, version 10. Only the tables needed for a bind-pose preview are
// read; animation data, bone controllers, hit boxes and attachments stay untouched.
constexpr std::uint32_t StudioIdentifier = 0x54534449; // "IDST"
constexpr std::uint32_t StudioVersion = 10;
constexpr std::size_t HeaderSize = 244;
constexpr std::size_t BoneSize = 112;
constexpr std::size_t TextureSize = 80;
constexpr std::size_t BodyPartSize = 76;
constexpr std::size_t ModelSize = 112;
constexpr std::size_t MeshSize = 20;

constexpr std::size_t SequenceSize = 176;
constexpr std::size_t AnimationSize = 12;

constexpr std::size_t MaxBones = 2048;
constexpr std::size_t MaxSequences = 8192;
constexpr std::size_t MaxTextures = 4096;
constexpr std::size_t MaxBodyParts = 512;
constexpr std::size_t MaxVariants = 256;
constexpr std::size_t MaxVertices = 400000;
constexpr std::size_t MaxTriangles = 400000;

bool contains(const Bytes& bytes, const std::size_t offset, const std::size_t size)
{
  return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::uint32_t readu32(const Bytes& bytes, const std::size_t offset)
{
  return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8)
         | (std::uint32_t{bytes[offset + 2]} << 16)
         | (std::uint32_t{bytes[offset + 3]} << 24);
}

std::int32_t readi32(const Bytes& bytes, const std::size_t offset)
{
  return static_cast<std::int32_t>(readu32(bytes, offset));
}

std::uint16_t readu16(const Bytes& bytes, const std::size_t offset)
{
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[offset])
    | static_cast<std::uint16_t>(std::uint16_t{bytes[offset + 1]} << 8));
}

std::int16_t readi16(const Bytes& bytes, const std::size_t offset)
{
  const auto low = static_cast<std::uint16_t>(bytes[offset]);
  const auto high = static_cast<std::uint16_t>(std::uint16_t{bytes[offset + 1]} << 8);
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(low | high));
}

float readf32(const Bytes& bytes, const std::size_t offset)
{
  const auto raw = readu32(bytes, offset);
  auto value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

std::string readFixedString(
  const Bytes& bytes, const std::size_t offset, const std::size_t size)
{
  const auto* text = reinterpret_cast<const char*>(bytes.data() + offset);
  return std::string{text, ::strnlen(text, size)};
}

struct Header;

/** Smallest box containing every authored vertex of every body variant. */
std::optional<vm::bbox3f> measureBounds(
  const Bytes& bytes, const Header& header);

struct Header
{
  std::string name;
  std::size_t bones = 0;
  std::size_t boneOffset = 0;
  std::size_t sequences = 0;
  std::size_t sequenceOffset = 0;
  std::size_t sequenceGroups = 1;
  std::size_t textures = 0;
  std::size_t textureOffset = 0;
  std::size_t skinReferences = 0;
  std::size_t skinFamilies = 0;
  std::size_t bodyParts = 0;
  std::size_t bodyPartOffset = 0;
  vm::bbox3f bounds;
};

std::size_t countAt(
  const Bytes& bytes, const std::size_t offset, const std::size_t maximum)
{
  const auto value = readi32(bytes, offset);
  return value > 0 && std::size_t(value) <= maximum ? std::size_t(value) : std::size_t{0};
}

std::size_t tableAt(
  const Bytes& bytes,
  const std::size_t offset,
  const std::size_t count,
  const std::size_t entrySize)
{
  const auto value = readi32(bytes, offset);
  if (value < 0)
    return 0;
  const auto start = std::size_t(value);
  const auto total = count * entrySize;
  return count > 0 && contains(bytes, start, total) ? start : 0;
}

std::optional<vm::bbox3f> measureBounds(const Bytes& bytes, const Header& header)
{
  auto low = std::optional<vm::vec3f>{};
  auto high = std::optional<vm::vec3f>{};
  const auto extend = [&](const vm::vec3f& point) {
    if (!low || !high)
    {
      low = point;
      high = point;
      return;
    }
    for (auto axis = std::size_t{0}; axis < 3; ++axis)
    {
      (*low)[axis] = std::min((*low)[axis], point[axis]);
      (*high)[axis] = std::max((*high)[axis], point[axis]);
    }
  };

  for (auto part = std::size_t{0}; part < header.bodyParts; ++part)
  {
    const auto partOffset = header.bodyPartOffset + part * BodyPartSize;
    const auto variantCount = countAt(bytes, partOffset + 64, MaxVariants);
    const auto models = tableAt(bytes, partOffset + 72, variantCount, ModelSize);
    if (variantCount == 0 || models == 0)
      continue;
    for (auto variant = std::size_t{0}; variant < variantCount; ++variant)
    {
      const auto modelOffset = models + variant * ModelSize;
      const auto vertexCount = countAt(bytes, modelOffset + 80, MaxVertices);
      const auto vertexOffset = tableAt(bytes, modelOffset + 88, vertexCount, 12);
      if (vertexCount == 0 || vertexOffset == 0)
        continue;
      for (auto vertex = std::size_t{0}; vertex < vertexCount; ++vertex)
        extend(
          {readf32(bytes, vertexOffset + vertex * 12),
           readf32(bytes, vertexOffset + vertex * 12 + 4),
           readf32(bytes, vertexOffset + vertex * 12 + 8)});
    }
  }

  if (!low || !high)
    return std::nullopt;
  return vm::bbox3f{*low, *high};
}

Result<Header> readHeader(const Bytes& bytes)
{
  if (bytes.size() < HeaderSize)
    return Error{"The studio model header is truncated."};
  if (readu32(bytes, 0) != StudioIdentifier)
    return Error{"Not a GoldSrc studio model."};
  if (readu32(bytes, 4) != StudioVersion)
    return Error{fmt::format("Unsupported studio model version {}.", readu32(bytes, 4))};

  auto header = Header{};
  header.name = readFixedString(bytes, 8, 64);
  header.bones = countAt(bytes, 140, MaxBones);
  header.boneOffset = tableAt(bytes, 144, header.bones, BoneSize);
  header.sequences = countAt(bytes, 164, MaxSequences);
  header.sequenceOffset = tableAt(bytes, 168, header.sequences, SequenceSize);
  header.sequenceGroups = std::max(std::size_t{1}, countAt(bytes, 172, 1024));
  header.textures = countAt(bytes, 180, MaxTextures);
  header.textureOffset = tableAt(bytes, 184, header.textures, TextureSize);
  header.skinReferences = countAt(bytes, 192, 4096);
  header.skinFamilies = countAt(bytes, 196, 4096);
  header.bodyParts = countAt(bytes, 204, MaxBodyParts);
  header.bodyPartOffset = tableAt(bytes, 208, header.bodyParts, BodyPartSize);

  if (header.bones > 0 && header.boneOffset == 0)
    return Error{"The studio model bone table lies outside the file."};
  if (header.textures > 0 && header.textureOffset == 0)
    return Error{"The studio model texture table lies outside the file."};
  if (header.bodyParts > 0 && header.bodyPartOffset == 0)
    return Error{"The studio model body part table lies outside the file."};
  if (header.sequences > 0 && header.sequenceOffset == 0)
    return Error{"The studio model sequence table lies outside the file."};

  auto low = vm::vec3f{readf32(bytes, 88), readf32(bytes, 92), readf32(bytes, 96)};
  auto high = vm::vec3f{readf32(bytes, 100), readf32(bytes, 104), readf32(bytes, 108)};
  for (auto axis = std::size_t{0}; axis < 3; ++axis)
  {
    if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]))
      return Error{"The studio model bounds are invalid."};
    if (low[axis] > high[axis])
      std::swap(low[axis], high[axis]);
  }
  header.bounds = vm::bbox3f{low, high};
  // Engine-authored models sometimes ship a zero render box; the vertex tables still
  // describe the authored volume, so fall back to them instead of reporting a point.
  if (header.bounds.size().x() <= 0.0f && header.bounds.size().y() <= 0.0f
      && header.bounds.size().z() <= 0.0f)
  {
    if (const auto measured = measureBounds(bytes, header); measured)
      header.bounds = *measured;
  }
  return header;
}

struct Transform
{
  std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::array<float, 3> translation{0, 0, 0};
};

// HLSDK AngleQuaternion: values 0..2 are the position and 3..5 the Euler angles in
// radians, so one helper serves both the rest pose and a decoded animation frame.
Transform transformFromValues(const std::array<float, 6>& values)
{
  const auto position = vm::vec3f{values[0], values[1], values[2]};
  const auto rx = values[3] * 0.5f;
  const auto ry = values[4] * 0.5f;
  const auto rz = values[5] * 0.5f;

  const auto sr = std::sin(rx), cr = std::cos(rx);
  const auto sp = std::sin(ry), cp = std::cos(ry);
  const auto sy = std::sin(rz), cy = std::cos(rz);

  const auto qx = sr * cp * cy - cr * sp * sy;
  const auto qy = cr * sp * cy + sr * cp * sy;
  const auto qz = cr * cp * sy - sr * sp * cy;
  const auto qw = cr * cp * cy + sr * sp * sy;

  auto result = Transform{};
  result.rotation = {
    1.0f - 2.0f * qy * qy - 2.0f * qz * qz,
    2.0f * qx * qy - 2.0f * qw * qz,
    2.0f * qx * qz + 2.0f * qw * qy,
    2.0f * qx * qy + 2.0f * qw * qz,
    1.0f - 2.0f * qx * qx - 2.0f * qz * qz,
    2.0f * qy * qz - 2.0f * qw * qx,
    2.0f * qx * qz - 2.0f * qw * qy,
    2.0f * qy * qz + 2.0f * qw * qx,
    1.0f - 2.0f * qx * qx - 2.0f * qy * qy};
  result.translation = {position.x(), position.y(), position.z()};
  return result;
}

Transform boneRestTransform(const Bytes& bytes, const std::size_t offset)
{
  auto values = std::array<float, 6>{};
  for (auto channel = std::size_t{0}; channel < 6; ++channel)
    values[channel] = readf32(bytes, offset + 64 + channel * 4);
  return transformFromValues(values);
}

vm::vec3f applyTransform(const Transform& transform, const vm::vec3f& point)
{
  // HLSDK stores row-major matrices and multiplies row vectors: out = p * R + t.
  const auto& r = transform.rotation;
  return {
    r[0] * point.x() + r[1] * point.y() + r[2] * point.z() + transform.translation[0],
    r[3] * point.x() + r[4] * point.y() + r[5] * point.z() + transform.translation[1],
    r[6] * point.x() + r[7] * point.y() + r[8] * point.z() + transform.translation[2]};
}

Transform compose(const Transform& parent, const Transform& child)
{
  // A bone's local frame is expressed in its parent's space: rotate by the parent first,
  // then add the bone's own translation expressed through the parent rotation.
  auto result = Transform{};
  for (auto row = std::size_t{0}; row < 3; ++row)
    for (auto column = std::size_t{0}; column < 3; ++column)
    {
      auto value = 0.0f;
      for (auto inner = std::size_t{0}; inner < 3; ++inner)
        value += parent.rotation[row * 3 + inner] * child.rotation[inner * 3 + column];
      result.rotation[row * 3 + column] = value;
    }
  // HLSDK ConcatTransforms: translation = parent rotation * child translation + parent.
  for (auto row = std::size_t{0}; row < 3; ++row)
  {
    auto value = parent.translation[row];
    for (auto inner = std::size_t{0}; inner < 3; ++inner)
      value += parent.rotation[row * 3 + inner] * child.translation[inner];
    result.translation[row] = value;
  }
  return result;
}

/** Per-bone angles and positions of one animation frame. */
struct BonePose
{
  std::array<float, 6> values{};
};

/**
 * Reads one animated channel of a bone.
 *
 * The stream stores runs of frames: a header with the number of valid values and the
 * number of frames it covers, followed by that many int16 values. A run that does not
 * cover the requested frame is skipped, which is how the engine plays sparse tracks.
 */
std::optional<float> readAnimationValue(
  const Bytes& bytes, const std::size_t offset, const std::size_t frame)
{
  auto position = offset;
  auto remaining = frame;
  for (auto guard = std::size_t{0}; guard < 4096; ++guard)
  {
    if (!contains(bytes, position, 2))
      return std::nullopt;
    const auto valid = std::size_t{bytes[position]};
    const auto total = std::size_t{bytes[position + 1]};
    if (total == 0)
      return std::nullopt;
    if (total > remaining)
    {
      if (valid > remaining && contains(bytes, position + 2 + remaining * 2, 2))
        return float(readi16(bytes, position + 2 + remaining * 2));
      return std::nullopt;
    }
    remaining -= total;
    position += 2 + valid * 2;
  }
  return std::nullopt;
}

/** Decodes the pose of one frame of a sequence, or nothing when the data is unusable. */
std::optional<std::vector<BonePose>> readSequencePose(
  const Bytes& bytes, const Header& header, const std::size_t sequence, const std::size_t frame)
{
  if (sequence >= header.sequences || header.sequenceOffset == 0)
    return std::nullopt;

  const auto offset = header.sequenceOffset + sequence * SequenceSize;
  const auto frameCount = countAt(bytes, offset + 56, 100000);
  const auto blends = countAt(bytes, offset + 120, 16);
  const auto animIndex = readi32(bytes, offset + 124);
  const auto group = readi32(bytes, offset + 156);
  // Only the embedded sequence group is available; external _T.mdl groups live in
  // another file that the browser does not open.
  if (frameCount == 0 || blends == 0 || animIndex <= 0 || group != 0)
    return std::nullopt;
  if (frame >= frameCount)
    return std::nullopt;

  const auto animationOffset = std::size_t(animIndex);
  if (!contains(bytes, animationOffset, blends * header.bones * AnimationSize))
    return std::nullopt;

  auto pose = std::vector<BonePose>(header.bones, BonePose{});
  for (auto bone = std::size_t{0}; bone < header.bones; ++bone)
  {
    const auto boneOffset = header.boneOffset + bone * BoneSize;
    const auto block = animationOffset + bone * AnimationSize;
    for (auto channel = std::size_t{0}; channel < 6; ++channel)
    {
      const auto channelOffset = std::size_t{readu16(bytes, block + channel * 2)};
      const auto base = readf32(bytes, boneOffset + 64 + channel * 4);
      const auto scale = readf32(bytes, boneOffset + 88 + channel * 4);
      pose[bone].values[channel] = base;
      if (channelOffset != 0)
      {
        if (const auto value = readAnimationValue(bytes, block + channelOffset, frame);
            value)
          pose[bone].values[channel] = base + scale * *value;
      }
    }
  }
  return pose;
}

std::vector<Transform> readBoneTransforms(
  const Bytes& bytes, const Header& header, const std::optional<std::vector<BonePose>>& pose)
{
  auto local = std::vector<Transform>(header.bones, Transform{});
  for (auto i = std::size_t{0}; i < header.bones; ++i)
    local[i] = pose && i < pose->size() ? transformFromValues((*pose)[i].values)
                                        : boneRestTransform(bytes, header.boneOffset + i * BoneSize);

  auto world = std::vector<Transform>(header.bones, Transform{});
  // Parents precede children in every engine-authored model; resolve in passes so a
  // malformed parent index cannot recurse forever.
  auto resolved = std::vector<bool>(header.bones, false);
  for (auto pass = std::size_t{0}; pass < header.bones; ++pass)
  {
    auto changed = false;
    for (auto i = std::size_t{0}; i < header.bones; ++i)
    {
      if (resolved[i])
        continue;
      const auto parent = readi32(bytes, header.boneOffset + i * BoneSize + 32);
      if (parent < 0 || std::size_t(parent) >= header.bones)
      {
        world[i] = local[i];
        resolved[i] = true;
        changed = true;
      }
      else if (resolved[std::size_t(parent)])
      {
        world[i] = compose(world[std::size_t(parent)], local[i]);
        resolved[i] = true;
        changed = true;
      }
    }
    if (!changed)
      break;
  }
  return world;
}

} // namespace

bool isGoldSrcStudioModel(const std::vector<std::uint8_t>& bytes)
{
  return bytes.size() >= 8 && readu32(bytes, 0) == StudioIdentifier
         && readu32(bytes, 4) == StudioVersion;
}

Result<GoldSrcStudioModelInfo> readGoldSrcStudioModelInfo(
  const std::vector<std::uint8_t>& bytes)
{
  auto header = readHeader(bytes);
  if (header.is_error())
    return std::get<Error>(header.error());
  const auto& value = header.value();

  auto result = GoldSrcStudioModelInfo{};
  result.name = value.name;
  result.bounds = value.bounds;
  result.bones = value.bones;
  result.sequenceGroups = value.sequenceGroups;
  result.sequences = value.sequences;
  result.skinFamilies = value.skinFamilies;
  result.skinReferences = value.skinReferences;

  for (auto i = std::size_t{0}; i < value.textures; ++i)
    result.textures.push_back(
      readFixedString(bytes, value.textureOffset + i * TextureSize, 64));

  for (auto i = std::size_t{0}; i < value.sequences; ++i)
  {
    const auto offset = value.sequenceOffset + i * SequenceSize;
    auto sequence = GoldSrcStudioSequence{};
    sequence.label = readFixedString(bytes, offset, 32);
    sequence.fps = readf32(bytes, offset + 32);
    sequence.looping = (readi32(bytes, offset + 36) & 1) != 0;
    sequence.frameCount = countAt(bytes, offset + 56, 100000);
    result.sequenceList.push_back(std::move(sequence));
  }

  for (auto part = std::size_t{0}; part < value.bodyParts; ++part)
  {
    const auto offset = value.bodyPartOffset + part * BodyPartSize;
    auto bodyPart = GoldSrcStudioBodyPart{};
    bodyPart.name = readFixedString(bytes, offset, 64);
    const auto variants = countAt(bytes, offset + 64, MaxVariants);
    const auto modelIndex = readi32(bytes, offset + 72);
    const auto models = tableAt(bytes, offset + 72, variants, ModelSize);
    if (modelIndex < 0 || variants == 0 || models == 0)
    {
      result.bodyParts.push_back(std::move(bodyPart));
      continue;
    }
    for (auto variant = std::size_t{0}; variant < variants; ++variant)
      bodyPart.variants.push_back(
        readFixedString(bytes, models + variant * ModelSize, 64));
    result.bodyParts.push_back(std::move(bodyPart));
  }
  return result;
}

Result<GoldSrcStudioMesh> buildGoldSrcStudioMesh(
  const std::vector<std::uint8_t>& bytes,
  const std::vector<std::size_t>& variants,
  const std::size_t sequence,
  const std::size_t frame)
{
  auto header = readHeader(bytes);
  if (header.is_error())
    return std::get<Error>(header.error());
  const auto& value = header.value();
  if (value.bodyParts == 0)
    return Error{"The studio model has no body parts."};

  const auto pose = readSequencePose(bytes, value, sequence, frame);
  const auto bones = readBoneTransforms(bytes, value, pose);
  auto result = GoldSrcStudioMesh{};

  for (auto part = std::size_t{0}; part < value.bodyParts; ++part)
  {
    const auto partOffset = value.bodyPartOffset + part * BodyPartSize;
    const auto variantCount = countAt(bytes, partOffset + 64, MaxVariants);
    const auto models = tableAt(bytes, partOffset + 72, variantCount, ModelSize);
    if (variantCount == 0 || models == 0)
      continue;

    const auto selected = part < variants.size() ? variants[part] : std::size_t{0};
    if (selected >= variantCount)
      return Error{fmt::format(
        "Variant {} does not exist for body part {}.", selected + 1, part + 1)};

    const auto modelOffset = models + selected * ModelSize;
    const auto meshCount = countAt(bytes, modelOffset + 72, 4096);
    const auto meshOffset = tableAt(bytes, modelOffset + 76, meshCount, MeshSize);
    const auto vertexCount = countAt(bytes, modelOffset + 80, MaxVertices);
    const auto vertexInfoOffset = tableAt(bytes, modelOffset + 84, vertexCount, 1);
    const auto vertexOffset = tableAt(bytes, modelOffset + 88, vertexCount, 12);
    if (
      meshCount == 0 || meshOffset == 0 || vertexCount == 0 || vertexInfoOffset == 0
      || vertexOffset == 0)
      continue;

    for (auto mesh = std::size_t{0}; mesh < meshCount; ++mesh)
    {
      const auto offset = meshOffset + mesh * MeshSize;
      const auto streamOffset = readi32(bytes, offset + 4);
      if (streamOffset < 0 || std::size_t(streamOffset) >= bytes.size())
        continue;

      // Each mesh stores runs of triangles: a positive count is a strip, a negative one
      // a fan, and zero terminates the mesh. Every vertex is four int16 values whose first
      // entry indexes the model's vertex array.
      auto stream = std::size_t(streamOffset);
      auto emitted = std::size_t{0};
      while (emitted < MaxTriangles * 3)
      {
        if (!contains(bytes, stream, 2))
          break;
        const auto count = readi16(bytes, stream);
        stream += 2;
        if (count == 0)
          break;

        const auto vertexCountInRun = std::size_t(std::abs(count));
        if (
          vertexCountInRun < 3 || vertexCountInRun > MaxVertices
          || !contains(bytes, stream, vertexCountInRun * 8))
          break;

        auto runVertices = std::vector<std::size_t>{};
        runVertices.reserve(vertexCountInRun);
        for (auto k = std::size_t{0}; k < vertexCountInRun; ++k)
        {
          const auto vertex = readi16(bytes, stream + k * 8);
          runVertices.push_back(
            vertex >= 0 && std::size_t(vertex) < vertexCount ? std::size_t(vertex)
                                                             : vertexCount);
        }
        stream += vertexCountInRun * 8;

        for (auto k = std::size_t{2}; k < vertexCountInRun; ++k)
        {
          // Strips alternate their winding; fans share their first vertex.
          const auto first = count > 0 ? (k % 2 == 0 ? k - 2 : k - 1) : std::size_t{0};
          const auto second = count > 0 ? (k % 2 == 0 ? k - 1 : k - 2) : k - 1;
          const auto base = result.positions.size();
          auto skipped = false;
          for (const auto index : {runVertices[first], runVertices[second], runVertices[k]})
          {
            // Retail models can carry stale indices; skip that triangle instead of
            // discarding the whole model.
            if (index >= vertexCount)
            {
              skipped = true;
              break;
            }
            const auto bone = bytes[vertexInfoOffset + index];
            const auto position = vm::vec3f{
              readf32(bytes, vertexOffset + index * 12),
              readf32(bytes, vertexOffset + index * 12 + 4),
              readf32(bytes, vertexOffset + index * 12 + 8)};
            result.positions.push_back(
              bone < bones.size() ? applyTransform(bones[bone], position) : position);
          }
          if (skipped)
          {
            result.positions.resize(base);
            continue;
          }
          result.triangles.push_back({base, base + 1, base + 2});
          emitted += 3;
        }
      }
    }
  }

  if (result.triangles.empty())
    return Error{"The studio model has no readable geometry."};

  return result;
}

} // namespace tb::mdl
