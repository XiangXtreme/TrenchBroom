/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#include "mdl/CatchConfig.h"
#include "mdl/GoldSrcStudioModel.h"

#include "kd/result.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace tb::mdl
{
namespace
{

struct StudioHeader
{
  int32_t id{};
  int32_t version{};
  char name[64]{};
  int32_t length{};
  float eyeposition[3]{};
  float min[3]{};
  float max[3]{};
  float bbmin[3]{};
  float bbmax[3]{};
  int32_t flags{};
  int32_t numbones{};
  int32_t boneindex{};
  int32_t numbonecontrollers{};
  int32_t bonecontrollerindex{};
  int32_t numhitboxes{};
  int32_t hitboxindex{};
  int32_t numseq{};
  int32_t seqindex{};
  int32_t numseqgroups{};
  int32_t seqgroupindex{};
  int32_t numtextures{};
  int32_t textureindex{};
  int32_t texturedataindex{};
  int32_t numskinref{};
  int32_t numskinfamilies{};
  int32_t skinindex{};
  int32_t numbodyparts{};
  int32_t bodypartindex{};
  int32_t numattachments{};
  int32_t attachmentindex{};
  int32_t soundtable{};
  int32_t soundindex{};
  int32_t soundgroups{};
  int32_t soundgroupindex{};
  int32_t numtransitions{};
  int32_t transitionindex{};
};

struct StudioBone
{
  char name[32]{};
  int32_t parent{};
  int32_t flags{};
  int32_t bonecontroller[6]{};
  float value[6]{};
  float scale[6]{};
};

struct StudioTexture
{
  char name[64]{};
  int32_t flags{};
  int32_t width{};
  int32_t height{};
  int32_t index{};
};

struct StudioBodyPart
{
  char name[64]{};
  int32_t nummodels{};
  int32_t base{};
  int32_t modelindex{};
};

struct StudioModel
{
  char name[64]{};
  int32_t type{};
  float boundingradius{};
  int32_t nummesh{};
  int32_t meshindex{};
  int32_t numverts{};
  int32_t vertinfoindex{};
  int32_t vertindex{};
  int32_t numnorms{};
  int32_t norminfoindex{};
  int32_t normindex{};
  int32_t numgroups{};
  int32_t groupindex{};
};

struct StudioMesh
{
  int32_t numtris{};
  int32_t triindex{};
  int32_t skinref{};
  int32_t numnorms{};
  int32_t normindex{};
};

struct StudioTriVertex
{
  int16_t vertex[3]{};
  int16_t normal{};
};

void writeAt(
  std::vector<std::uint8_t>& data,
  const std::size_t offset,
  const void* value,
  const std::size_t size)
{
  if (data.size() < offset + size)
    data.resize(offset + size);
  std::memcpy(data.data() + offset, value, size);
}

template <typename T>
void writeAt(std::vector<std::uint8_t>& data, const std::size_t offset, const T& value)
{
  writeAt(data, offset, &value, sizeof(T));
}

/**
 * One body part with two variants. Variant 0 places its single vertex on the second bone,
 * which is offset by ten units; variant 1 keeps the vertex on the root bone. That makes
 * bone composition and variant selection observable in the resulting mesh.
 */
std::vector<std::uint8_t> makeStudioModel(const float boneAngleZ = 0.0f)
{
  const auto headerSize = sizeof(StudioHeader);
  const auto boneOffset = headerSize;
  const auto textureOffset = boneOffset + 2 * sizeof(StudioBone);
  const auto bodyPartOffset = textureOffset + sizeof(StudioTexture);
  const auto modelOffset = bodyPartOffset + sizeof(StudioBodyPart);
  const auto meshOffset = modelOffset + 2 * sizeof(StudioModel);
  const auto triangleOffset = meshOffset + 2 * sizeof(StudioMesh);
  const auto vertexInfoOffset = triangleOffset + 2 * sizeof(StudioTriVertex);
  const auto vertexOffset = vertexInfoOffset + 2;
  const auto totalSize = vertexOffset + 2 * 6;

  auto data = std::vector<std::uint8_t>{};
  auto header = StudioHeader{};
  header.id = 0x54534449; // "IDST"
  header.version = 10;
  std::strncpy(header.name, "testmodel", sizeof(header.name) - 1);
  header.min[0] = -8.0f;
  header.min[1] = -8.0f;
  header.min[2] = -8.0f;
  header.max[0] = 16.0f;
  header.max[1] = 8.0f;
  header.max[2] = 8.0f;
  header.numbones = 2;
  header.boneindex = int32_t(boneOffset);
  header.numseq = 3;
  header.numseqgroups = 2;
  header.numtextures = 1;
  header.textureindex = int32_t(textureOffset);
  header.numskinref = 1;
  header.numskinfamilies = 2;
  header.numbodyparts = 1;
  header.bodypartindex = int32_t(bodyPartOffset);
  writeAt(data, 0, header);
  data.resize(totalSize);

  auto rootBone = StudioBone{};
  std::strncpy(rootBone.name, "root", sizeof(rootBone.name) - 1);
  rootBone.parent = -1;
  writeAt(data, boneOffset, rootBone);

  auto childBone = StudioBone{};
  std::strncpy(childBone.name, "child", sizeof(childBone.name) - 1);
  childBone.parent = 0;
  childBone.value[0] = 10.0f;
  childBone.value[5] = boneAngleZ;
  writeAt(data, boneOffset + sizeof(StudioBone), childBone);

  auto texture = StudioTexture{};
  std::strncpy(texture.name, "brick", sizeof(texture.name) - 1);
  texture.width = 64;
  texture.height = 64;
  writeAt(data, textureOffset, texture);

  auto bodyPart = StudioBodyPart{};
  std::strncpy(bodyPart.name, "body", sizeof(bodyPart.name) - 1);
  bodyPart.nummodels = 2;
  bodyPart.base = 0;
  bodyPart.modelindex = int32_t(modelOffset);
  writeAt(data, bodyPartOffset, bodyPart);

  for (auto variant = 0; variant < 2; ++variant)
  {
    auto model = StudioModel{};
    std::strncpy(model.name, variant == 0 ? "body_a" : "body_b", sizeof(model.name) - 1);
    model.nummesh = 1;
    model.meshindex = int32_t(meshOffset + variant * sizeof(StudioMesh));
    model.numverts = 1;
    model.vertinfoindex = int32_t(vertexInfoOffset + variant);
    model.vertindex = int32_t(vertexOffset + variant * 6);
    writeAt(data, modelOffset + variant * sizeof(StudioModel), model);

    auto mesh = StudioMesh{};
    mesh.numtris = 1;
    mesh.triindex = int32_t(triangleOffset + variant * sizeof(StudioTriVertex));
    writeAt(data, meshOffset + variant * sizeof(StudioMesh), mesh);

    auto triangle = StudioTriVertex{};
    triangle.vertex[0] = 0;
    triangle.vertex[1] = 0;
    triangle.vertex[2] = 0;
    writeAt(data, triangleOffset + variant * sizeof(StudioTriVertex), triangle);

    const auto bone = static_cast<std::uint8_t>(variant == 0 ? 1 : 0);
    writeAt(data, vertexInfoOffset + variant, bone);

    const auto x = static_cast<std::int16_t>(variant == 0 ? 1 : 2);
    writeAt(data, vertexOffset + variant * 6, x);
  }

  return data;
}

} // namespace

TEST_CASE("GoldSrcStudioModel")
{
  SECTION("identifies version 10 studio models")
  {
    CHECK(isGoldSrcStudioModel(makeStudioModel()));
    CHECK_FALSE(isGoldSrcStudioModel({}));
    CHECK_FALSE(isGoldSrcStudioModel(std::vector<std::uint8_t>(244, 0)));
  }

  SECTION("reads skeleton, body variants, skins and embedded textures")
  {
    const auto info = readGoldSrcStudioModelInfo(makeStudioModel());
    REQUIRE(info.is_success());
    const auto& model = info.value();
    CHECK(model.name == "testmodel");
    CHECK(model.bones == 2);
    CHECK(model.sequences == 3);
    CHECK(model.sequenceGroups == 2);
    CHECK(model.skinFamilies == 2);
    CHECK(model.skinReferences == 1);
    CHECK(model.textures == std::vector<std::string>{"brick"});
    REQUIRE(model.bodyParts.size() == 1);
    CHECK(model.bodyParts.front().name == "body");
    CHECK(
      model.bodyParts.front().variants == std::vector<std::string>{"body_a", "body_b"});
    CHECK(model.bounds.min == vm::vec3f{-8.0f, -8.0f, -8.0f});
    CHECK(model.bounds.max == vm::vec3f{16.0f, 8.0f, 8.0f});
  }

  SECTION("builds bind-pose geometry and switches body variants")
  {
    const auto bytes = makeStudioModel();
    const auto first = buildGoldSrcStudioMesh(bytes);
    REQUIRE(first.is_success());
    REQUIRE(first.value().positions.size() == 3);
    CHECK(first.value().positions.front() == vm::vec3f{11.0f, 0.0f, 0.0f});

    const auto second = buildGoldSrcStudioMesh(bytes, {1});
    REQUIRE(second.is_success());
    CHECK(second.value().positions.front() == vm::vec3f{2.0f, 0.0f, 0.0f});

    CHECK(buildGoldSrcStudioMesh(bytes, {2}).is_error());
  }

  SECTION("composes bone rotations in the rest pose")
  {
    const auto bytes = makeStudioModel(3.14159265f / 2.0f);
    const auto mesh = buildGoldSrcStudioMesh(bytes);
    REQUIRE(mesh.is_success());
    const auto position = mesh.value().positions.front();
    CHECK_THAT(position.x(), Catch::Matchers::WithinAbs(10.0f, 0.001));
    CHECK_THAT(position.y(), Catch::Matchers::WithinAbs(1.0f, 0.001));
    CHECK_THAT(position.z(), Catch::Matchers::WithinAbs(0.0f, 0.001));
  }

  SECTION("rejects tables outside the file and missing geometry")
  {
    auto truncated = makeStudioModel();
    truncated.resize(sizeof(StudioHeader) + 8);
    CHECK(readGoldSrcStudioModelInfo(truncated).is_error());
    CHECK(buildGoldSrcStudioMesh(truncated).is_error());

    auto empty = makeStudioModel();
    auto header = *reinterpret_cast<const StudioHeader*>(empty.data());
    header.numbodyparts = 0;
    writeAt(empty, 0, header);
    CHECK(buildGoldSrcStudioMesh(empty).is_error());
  }
}

} // namespace tb::mdl
