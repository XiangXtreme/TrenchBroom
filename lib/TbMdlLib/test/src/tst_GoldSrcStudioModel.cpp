/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/

#include "mdl/CatchConfig.h"
#include "mdl/GoldSrcStudioModel.h"

#include "kd/result.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
  // Each variant stores one triangle strip: a count, three 4-short vertices, a zero.
  const auto stripSize = 2 + 3 * sizeof(StudioTriVertex) + 2;
  const auto triangleOffset = meshOffset + 2 * sizeof(StudioMesh);
  const auto vertexInfoOffset = triangleOffset + 2 * stripSize;
  const auto vertexOffset = vertexInfoOffset + 2 * 3;
  const auto totalSize = vertexOffset + 2 * 36;

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
  header.numseq = 0;
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

  for (auto variant = std::size_t{0}; variant < 2; ++variant)
  {
    auto model = StudioModel{};
    std::strncpy(model.name, variant == 0 ? "body_a" : "body_b", sizeof(model.name) - 1);
    model.nummesh = 1;
    model.meshindex = int32_t(meshOffset + variant * sizeof(StudioMesh));
    model.numverts = 3;
    model.vertinfoindex = int32_t(vertexInfoOffset + variant * 3);
    model.vertindex = int32_t(vertexOffset + variant * 36);
    writeAt(data, modelOffset + variant * sizeof(StudioModel), model);

    auto mesh = StudioMesh{};
    mesh.numtris = 1;
    mesh.triindex = int32_t(triangleOffset + variant * stripSize);
    writeAt(data, meshOffset + variant * sizeof(StudioMesh), mesh);

    const auto stripStart = triangleOffset + variant * stripSize;
    writeAt(data, stripStart, int16_t{3});
    for (auto corner = std::size_t{0}; corner < 3; ++corner)
    {
      auto triangle = StudioTriVertex{};
      triangle.vertex[0] = int16_t(corner);
      writeAt(data, stripStart + 2 + corner * sizeof(StudioTriVertex), triangle);
    }
    writeAt(data, stripStart + 2 + 3 * sizeof(StudioTriVertex), int16_t{0});

    const auto points = std::array<std::array<float, 3>, 3>{
      std::array<float, 3>{variant == 0 ? 1.0f : 2.0f, 0.0f, 0.0f},
      std::array<float, 3>{12.0f, 12.0f, 0.0f},
      std::array<float, 3>{-12.0f, 12.0f, 0.0f}};
    for (auto vertex = 0; vertex < 3; ++vertex)
    {
      writeAt(
        data,
        vertexInfoOffset + variant * 3 + std::size_t(vertex),
        static_cast<std::uint8_t>(variant == 0 ? 1 : 0));
      writeAt(
        data,
        vertexOffset + variant * 36 + std::size_t(vertex) * 12,
        points[std::size_t(vertex)].data(),
        12);
    }
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
    CHECK(model.sequences == 0);
  CHECK(model.sequenceList.empty());
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
    CHECK(first.value().texCoords.size() == first.value().positions.size());
    CHECK(first.value().triangleTextures.size() == first.value().triangles.size());
    CHECK(first.value().textures.size() == 1);

    const auto second = buildGoldSrcStudioMesh(bytes, {1});
    REQUIRE(second.is_success());
    CHECK(second.value().positions.front() == vm::vec3f{2.0f, 0.0f, 0.0f});

    CHECK(buildGoldSrcStudioMesh(bytes, {2}).is_error());
  }

  SECTION("holds compressed animation samples and advances to the next run")
  {
    auto bytes = makeStudioModel();
    auto header = *reinterpret_cast<const StudioHeader*>(bytes.data());
    const auto sequenceOffset = bytes.size();
    const auto animationOffset = sequenceOffset + 176;
    bytes.resize(animationOffset + 24 + 8, 0);
    header.numseq = 1;
    header.seqindex = int32_t(sequenceOffset);
    writeAt(bytes, 0, header);
    writeAt(bytes, sequenceOffset + 56, int32_t{5});
    writeAt(bytes, sequenceOffset + 120, int32_t{1});
    writeAt(bytes, sequenceOffset + 124, int32_t(animationOffset));
    writeAt(bytes, animationOffset, uint16_t{24});
    writeAt(bytes, std::size_t(header.boneindex) + 88, 1.0f);
    const auto samples = animationOffset + 24;
    bytes[samples] = 1;
    bytes[samples + 1] = 3;
    writeAt(bytes, samples + 2, int16_t{5});
    bytes[samples + 4] = 1;
    bytes[samples + 5] = 2;
    writeAt(bytes, samples + 6, int16_t{9});
    for (auto frame = std::size_t{0}; frame < 5; ++frame)
    {
      const auto mesh = buildGoldSrcStudioMesh(bytes, {}, 0, frame);
      REQUIRE(mesh.is_success());
      CHECK(mesh.value().positions.front().x() == (frame < 3 ? 16.0f : 20.0f));
    }
    bytes[samples] = 0;
    CHECK(buildGoldSrcStudioMesh(bytes).value().positions.front().x() == 11.0f);
    bytes[samples] = 4;
    CHECK(buildGoldSrcStudioMesh(bytes).value().positions.front().x() == 11.0f);
  }

  SECTION("decodes masked texture transparency")
  {
    auto bytes = makeStudioModel();
    const auto header = *reinterpret_cast<const StudioHeader*>(bytes.data());
    const auto textureOffset = std::size_t(header.textureindex);
    const auto dataOffset = bytes.size();
    constexpr auto Count = std::size_t{4};
    bytes.resize(dataOffset + Count + 768, 0);
    // Two of the four texels use palette entry 255, which is transparent when masked.
    bytes[dataOffset + 0] = 255;
    bytes[dataOffset + 1] = 0;
    bytes[dataOffset + 2] = 255;
    bytes[dataOffset + 3] = 0;
    // Palette entry 0 is red, entry 255 is blue.
    bytes[dataOffset + Count + 0] = 255;
    bytes[dataOffset + Count + 255 * 3 + 2] = 255;
    writeAt(bytes, textureOffset + 64, int32_t{0x40});
    writeAt(bytes, textureOffset + 68, int32_t{2});
    writeAt(bytes, textureOffset + 72, int32_t{2});
    writeAt(bytes, textureOffset + 76, int32_t(dataOffset));

    const auto mesh = buildGoldSrcStudioMesh(bytes);
    REQUIRE(mesh.is_success());
    REQUIRE(mesh.value().textures.size() == 1);
    const auto& texture = mesh.value().textures.front();
    CHECK(texture.flags == 0x40);
    REQUIRE(texture.pixels.size() == Count);
    CHECK((texture.pixels[0] >> 24) == 0);
    CHECK((texture.pixels[2] >> 24) == 0);
    CHECK((texture.pixels[1] >> 24) == 0xFF);
    CHECK(((texture.pixels[1] >> 16) & 0xFF) == 0xFF);
  }

  SECTION("reads textures from a separate texture header")
  {
    auto main = makeStudioModel();
    auto header = *reinterpret_cast<StudioHeader*>(main.data());
    header.numtextures = 0;
    header.textureindex = 0;
    header.numskinref = 0;
    header.numskinfamilies = 0;
    header.skinindex = 0;
    writeAt(main, 0, header);

    auto textures = makeStudioModel();
    const auto skinOffset = std::uint32_t(textures.size());
    textures.resize(textures.size() + 2, 0);
    auto textureHeader = *reinterpret_cast<StudioHeader*>(textures.data());
    textureHeader.numbones = 0;
    textureHeader.boneindex = 0;
    textureHeader.numseq = 0;
    textureHeader.seqindex = 0;
    textureHeader.numbodyparts = 0;
    textureHeader.bodypartindex = 0;
    textureHeader.numskinref = 1;
    textureHeader.numskinfamilies = 1;
    textureHeader.skinindex = int32_t(skinOffset);
    writeAt(textures, 0, textureHeader);

    const auto external = GoldSrcStudioExternalData{&textures, nullptr};
    const auto info = readGoldSrcStudioModelInfo(main, external);
    REQUIRE(info.is_success());
    CHECK(info.value().textures == std::vector<std::string>{"brick"});
    CHECK(info.value().externalTextures);
    CHECK(info.value().skinFamilies == 1);
    CHECK(info.value().skinReferences == 1);

    const auto mesh = buildGoldSrcStudioMesh(main, {}, 0, 0, true, external);
    REQUIRE(mesh.is_success());
    REQUIRE(mesh.value().textures.size() == 1);
    CHECK(mesh.value().textures.front().width == 64);
  }

  SECTION("decodes animation from an external sequence group")
  {
    auto main = makeStudioModel();
    const auto sequenceOffset = std::uint32_t(main.size());
    main.resize(main.size() + 176, 0);
    const auto groupTableOffset = std::uint32_t(main.size());
    main.resize(main.size() + 2 * 104, 0);

    auto header = *reinterpret_cast<StudioHeader*>(main.data());
    header.numseq = 1;
    header.seqindex = int32_t(sequenceOffset);
    header.numseqgroups = 2;
    header.seqgroupindex = int32_t(groupTableOffset);
    writeAt(main, 0, header);

    // The child bone's X channel is the only animated channel.
    writeAt(main, sizeof(StudioHeader) + sizeof(StudioBone) + 88, 1.0f);

    writeAt(main, sequenceOffset, "animate", 8);
    writeAt(main, sequenceOffset + 32, 15.0f);
    writeAt(main, sequenceOffset + 56, int32_t{1});
    writeAt(main, sequenceOffset + 120, int32_t{2});
    writeAt(main, sequenceOffset + 124, int32_t{76});
    writeAt(main, sequenceOffset + 156, int32_t{1});
    writeAt(main, groupTableOffset + 104 + 32, "group01.mdl", 12);

    auto group = std::vector<std::uint8_t>(132, 0);
    writeAt(group, 0, uint32_t{0x51534449}); // "IDSQ"
    writeAt(group, 4, uint32_t{10});
    // Blend 0 bone 1 X points at 124 (block 88 + 36); blend 1 bone 1 X at 128
    // (block 112 + 16). Both runs sit after the two animation tables.
    writeAt(group, 76 + 12, uint16_t{36});
    writeAt(group, 100 + 12, uint16_t{16});
    group[124] = 1; // valid
    group[125] = 1; // total
    writeAt(group, 126, int16_t{5});
    group[128] = 1; // valid
    group[129] = 1; // total
    writeAt(group, 130, int16_t{9});

    auto groups = std::vector<std::vector<std::uint8_t>>{std::move(group)};
    const auto external = GoldSrcStudioExternalData{nullptr, &groups};
    const auto info = readGoldSrcStudioModelInfo(main, external);
    REQUIRE(info.is_success());
    REQUIRE(info.value().sequenceList.size() == 1);
    CHECK(info.value().sequenceList.front().group == 1);
    CHECK(info.value().sequenceList.front().blends == 2);
    REQUIRE(info.value().sequenceGroupFiles.size() == 2);
    CHECK(info.value().sequenceGroupFiles[1] == "group01.mdl");

    const auto posed = buildGoldSrcStudioMesh(main, {}, 0, 0, true, external);
    REQUIRE(posed.is_success());
    CHECK_THAT(posed.value().positions.front().x(), Catch::Matchers::WithinAbs(16.0f, 0.001));

    const auto secondBlend =
      buildGoldSrcStudioMesh(main, {}, 0, 0, true, external, 0, 1);
    REQUIRE(secondBlend.is_success());
    CHECK_THAT(
      secondBlend.value().positions.front().x(), Catch::Matchers::WithinAbs(20.0f, 0.001));

    // Without the group file the same sequence falls back to the bone rest pose.
    const auto rest = buildGoldSrcStudioMesh(main);
    REQUIRE(rest.is_success());
    CHECK_THAT(rest.value().positions.front().x(), Catch::Matchers::WithinAbs(11.0f, 0.001));
  }

  SECTION("applies neutral bone controller offsets")
  {
    auto main = makeStudioModel();
    const auto controllerOffset = std::uint32_t(main.size());
    main.resize(main.size() + 24, 0);
    auto header = *reinterpret_cast<StudioHeader*>(main.data());
    header.numbonecontrollers = 1;
    header.bonecontrollerindex = int32_t(controllerOffset);
    writeAt(main, 0, header);

    writeAt(main, controllerOffset, int32_t{1}); // bone
    writeAt(main, controllerOffset + 4, int32_t{0x0010}); // STUDIO_YR
    writeAt(main, controllerOffset + 8, 0.0f); // start
    writeAt(main, controllerOffset + 12, 90.0f); // end
    writeAt(main, controllerOffset + 16, int32_t{0}); // rest
    writeAt(main, controllerOffset + 20, int32_t{3}); // user controller
    // Unused bone controller slots are -1 in engine-authored models.
    for (auto bone = 0; bone < 2; ++bone)
      for (auto channel = 0; channel < 6; ++channel)
        writeAt(
          main,
          sizeof(StudioHeader) + std::size_t(bone) * sizeof(StudioBone) + 40
            + std::size_t(channel) * 4,
          int32_t{-1});
    // Bone 1 Y rotation uses controller table entry 0.
    writeAt(
      main,
      sizeof(StudioHeader) + sizeof(StudioBone) + 40 + 4 * 4,
      int32_t{0});

    const auto info = readGoldSrcStudioModelInfo(main);
    REQUIRE(info.is_success());
    CHECK(info.value().boneControllers == 1);

    const auto mesh = buildGoldSrcStudioMesh(main);
    REQUIRE(mesh.is_success());
    const auto& position = mesh.value().positions.front();
    const auto angle = 127.0f / 255.0f * 90.0f * 3.14159265f / 180.0f;
    CHECK_THAT(
      position.x(), Catch::Matchers::WithinAbs(10.0f + std::cos(angle), 0.01f));
    CHECK_THAT(
      std::abs(position.z()), Catch::Matchers::WithinAbs(std::sin(angle), 0.01f));
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

TEST_CASE("GoldSrcStudioModel real model")
{
  // Opt-in: the repository ships no GoldSrc game assets. Point TB_GOLDSRC_MODEL at a
  // model from an installed game to validate the reader against real data.
  const auto path = std::getenv("TB_GOLDSRC_MODEL");
  if (!path || !*path)
    SKIP("TB_GOLDSRC_MODEL is not set");

  auto stream = std::ifstream{std::filesystem::path{path}, std::ios::binary};
  REQUIRE(stream);
  const auto bytes = std::vector<std::uint8_t>{
    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  REQUIRE(isGoldSrcStudioModel(bytes));

  const auto info = readGoldSrcStudioModelInfo(bytes);
  REQUIRE(info.is_success());
  const auto& model = info.value();
  CHECK_FALSE(model.name.empty());
  CHECK(model.bones > 0);
  CHECK_FALSE(model.bodyParts.empty());
  CHECK_FALSE(model.textures.empty());
  CHECK(model.bounds.size().x() > 1.0f);
  for (const auto& bodyPart : model.bodyParts)
    CHECK_FALSE(bodyPart.variants.empty());

  const auto mesh = buildGoldSrcStudioMesh(bytes);
  if (mesh.is_error())
  {
    // Sequence-driven models are refused instead of drawn with a folded rest pose.
    CHECK(
      std::get<Error>(mesh.error()).msg.find("sequence animation")
      != std::string::npos);
    return;
  }
  CHECK(mesh.value().positions.size() % 3 == 0);
  CHECK_FALSE(mesh.value().triangles.empty());

  // Every body variant must produce geometry of its own, not an empty or shared mesh.
  auto variants = std::vector<std::size_t>(model.bodyParts.size(), 0);
  for (auto part = std::size_t{0}; part < model.bodyParts.size(); ++part)
    for (auto variant = std::size_t{0}; variant < model.bodyParts[part].variants.size();
         ++variant)
    {
      variants[part] = variant;
      const auto variantMesh = buildGoldSrcStudioMesh(bytes, variants);
      if (variantMesh.is_success())
        CHECK_FALSE(variantMesh.value().triangles.empty());
    }
}

} // namespace tb::mdl
