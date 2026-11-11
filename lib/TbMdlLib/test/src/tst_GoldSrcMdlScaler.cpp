/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdl/CatchConfig.h"
#include "mdl/GoldSrcMdlScaler.h"

#include "kd/result.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::io
{
namespace
{
class TestDir
{
private:
  std::filesystem::path m_path;

public:
  explicit TestDir(std::string name)
    : m_path{std::filesystem::current_path() / std::move(name)}
  {
    std::filesystem::remove_all(m_path);
    std::filesystem::create_directories(m_path);
  }

  ~TestDir() { std::filesystem::remove_all(m_path); }

  const std::filesystem::path& path() const { return m_path; }
};

struct GoldSrcStudioHdr
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

struct GoldSrcMStudioBone
{
  char name[32]{};
  int32_t parent{};
  int32_t flags{};
  int32_t bonecontroller[6]{};
  float value[6]{};
  float scale[6]{};
};

struct GoldSrcMStudioBoneController
{
  int32_t bone{};
  int32_t type{};
  float start{};
  float end{};
  int32_t rest{};
  int32_t index{};
};

struct GoldSrcMStudioHitbox
{
  int32_t bone{};
  int32_t group{};
  float bbmin[3]{};
  float bbmax[3]{};
};

struct GoldSrcMStudioAttachment
{
  char name[32]{};
  int32_t type{};
  int32_t bone{};
  float org[3]{};
  float vectors[3][3]{};
};

struct GoldSrcMStudioBodyPart
{
  char name[64]{};
  int32_t nummodels{};
  int32_t base{};
  int32_t modelindex{};
};

struct GoldSrcMStudioModel
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

template <typename T>
T& write(std::vector<uint8_t>& data, const size_t offset, const T& value)
{
  if (data.size() < offset + sizeof(T))
  {
    data.resize(offset + sizeof(T));
  }
  std::memcpy(data.data() + offset, &value, sizeof(T));
  return *reinterpret_cast<T*>(data.data() + offset);
}

template <typename T>
const T& read(const std::vector<uint8_t>& data, const size_t offset)
{
  return *reinterpret_cast<const T*>(data.data() + offset);
}

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path)
{
  auto stream = std::ifstream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data)
{
  auto stream = std::ofstream{path, std::ios::binary};
  stream.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

std::vector<uint8_t> makeMinimalGoldSrcMdl()
{
  auto data = std::vector<uint8_t>{};
  auto header = GoldSrcStudioHdr{};

  const auto boneOffset = sizeof(GoldSrcStudioHdr);
  const auto controllerOffset = boneOffset + sizeof(GoldSrcMStudioBone);
  const auto hitboxOffset = controllerOffset + sizeof(GoldSrcMStudioBoneController);
  const auto attachmentOffset = hitboxOffset + sizeof(GoldSrcMStudioHitbox);
  const auto bodyPartOffset = attachmentOffset + sizeof(GoldSrcMStudioAttachment);
  const auto modelOffset = bodyPartOffset + sizeof(GoldSrcMStudioBodyPart);
  const auto vertexOffset = modelOffset + sizeof(GoldSrcMStudioModel);
  const auto length = vertexOffset + sizeof(float) * 6u;

  header.id = 0x54534449;
  header.version = 10;
  header.length = int32_t(length);
  header.eyeposition[0] = 1.0f;
  header.eyeposition[1] = 2.0f;
  header.eyeposition[2] = 3.0f;
  header.min[0] = -1.0f;
  header.min[1] = -2.0f;
  header.min[2] = -3.0f;
  header.max[0] = 4.0f;
  header.max[1] = 5.0f;
  header.max[2] = 6.0f;
  header.bbmin[0] = -4.0f;
  header.bbmin[1] = -5.0f;
  header.bbmin[2] = -6.0f;
  header.bbmax[0] = 7.0f;
  header.bbmax[1] = 8.0f;
  header.bbmax[2] = 9.0f;
  header.numbones = 1;
  header.boneindex = int32_t(boneOffset);
  header.numbonecontrollers = 1;
  header.bonecontrollerindex = int32_t(controllerOffset);
  header.numhitboxes = 1;
  header.hitboxindex = int32_t(hitboxOffset);
  header.numattachments = 1;
  header.attachmentindex = int32_t(attachmentOffset);
  header.numbodyparts = 1;
  header.bodypartindex = int32_t(bodyPartOffset);
  write(data, 0, header);

  auto bone = GoldSrcMStudioBone{};
  bone.value[0] = 10.0f;
  bone.value[1] = 20.0f;
  bone.value[2] = 30.0f;
  bone.value[3] = 40.0f;
  bone.scale[0] = 1.5f;
  bone.scale[3] = 2.5f;
  bone.bonecontroller[0] = 0;
  write(data, boneOffset, bone);

  auto controller = GoldSrcMStudioBoneController{};
  controller.type = 0x0001; // STUDIO_X
  controller.start = -4.0f;
  controller.end = 8.0f;
  controller.index = 0;
  write(data, controllerOffset, controller);

  auto hitbox = GoldSrcMStudioHitbox{};
  hitbox.bbmin[0] = -10.0f;
  hitbox.bbmin[1] = -20.0f;
  hitbox.bbmin[2] = -30.0f;
  hitbox.bbmax[0] = 10.0f;
  hitbox.bbmax[1] = 20.0f;
  hitbox.bbmax[2] = 30.0f;
  write(data, hitboxOffset, hitbox);

  auto attachment = GoldSrcMStudioAttachment{};
  attachment.org[0] = 11.0f;
  attachment.org[1] = 12.0f;
  attachment.org[2] = 13.0f;
  attachment.vectors[0][0] = 99.0f;
  write(data, attachmentOffset, attachment);

  auto bodyPart = GoldSrcMStudioBodyPart{};
  bodyPart.nummodels = 1;
  bodyPart.modelindex = int32_t(modelOffset);
  write(data, bodyPartOffset, bodyPart);

  auto model = GoldSrcMStudioModel{};
  model.boundingradius = 25.0f;
  model.numverts = 2;
  model.vertindex = int32_t(vertexOffset);
  write(data, modelOffset, model);

  const float verts[6]{1.0f, 2.0f, 3.0f, -1.0f, -2.0f, -3.0f};
  data.resize(length);
  std::memcpy(data.data() + vertexOffset, verts, sizeof(verts));

  return data;
}
} // namespace

TEST_CASE("GoldSrcMdlScaler")
{
  auto testDir = TestDir{"GoldSrcMdlScalerTest"};

  SECTION("scales GoldSrc Studio MDL spatial data")
  {
    const auto path = testDir.path() / "model.mdl";
    writeBinaryFile(path, makeMinimalGoldSrcMdl());

    scaleGoldSrcMdlFile(path, 2.0f)
      | kdl::transform_error([](const auto& e) { FAIL(e.msg); });

    const auto data = readBinaryFile(path);
    const auto& header = read<GoldSrcStudioHdr>(data, 0);
    CHECK(header.eyeposition[0] == 2.0f);
    CHECK(header.eyeposition[1] == 4.0f);
    CHECK(header.eyeposition[2] == 6.0f);
    CHECK(header.min[0] == -2.0f);
    CHECK(header.max[2] == 12.0f);
    CHECK(header.bbmin[1] == -10.0f);
    CHECK(header.bbmax[2] == 18.0f);

    const auto& bone = read<GoldSrcMStudioBone>(data, size_t(header.boneindex));
    CHECK(bone.value[0] == 20.0f);
    CHECK(bone.value[1] == 40.0f);
    CHECK(bone.value[2] == 60.0f);
    CHECK(bone.value[3] == 40.0f);
    CHECK(bone.scale[0] == 3.0f);
    CHECK(bone.scale[3] == 2.5f);

    const auto& controller =
      read<GoldSrcMStudioBoneController>(data, size_t(header.bonecontrollerindex));
    CHECK(controller.start == -8.0f);
    CHECK(controller.end == 16.0f);

    const auto& hitbox = read<GoldSrcMStudioHitbox>(data, size_t(header.hitboxindex));
    CHECK(hitbox.bbmin[0] == -20.0f);
    CHECK(hitbox.bbmax[2] == 60.0f);

    const auto& attachment =
      read<GoldSrcMStudioAttachment>(data, size_t(header.attachmentindex));
    CHECK(attachment.org[0] == 22.0f);
    CHECK(attachment.org[2] == 26.0f);
    CHECK(attachment.vectors[0][0] == 99.0f);

    const auto& bodyPart =
      read<GoldSrcMStudioBodyPart>(data, size_t(header.bodypartindex));
    const auto& model = read<GoldSrcMStudioModel>(data, size_t(bodyPart.modelindex));
    CHECK(model.boundingradius == 50.0f);

    const auto* verts = reinterpret_cast<const float*>(data.data() + model.vertindex);
    CHECK(verts[0] == 2.0f);
    CHECK(verts[1] == 4.0f);
    CHECK(verts[2] == 6.0f);
    CHECK(verts[3] == -2.0f);
    CHECK(verts[4] == -4.0f);
    CHECK(verts[5] == -6.0f);
  }

  SECTION("rejects invalid scale")
  {
    const auto path = testDir.path() / "model.mdl";
    const auto originalData = makeMinimalGoldSrcMdl();
    writeBinaryFile(path, originalData);

    CHECK(scaleGoldSrcMdlFile(path, 0.0f).is_error());
    CHECK(readBinaryFile(path) == originalData);

    CHECK(scaleGoldSrcMdlFile(path, std::numeric_limits<float>::infinity()).is_error());
    CHECK(readBinaryFile(path) == originalData);
  }

  SECTION("rejects non GoldSrc Studio MDL file")
  {
    const auto path = testDir.path() / "invalid.mdl";
    auto data = makeMinimalGoldSrcMdl();
    auto& header = *reinterpret_cast<GoldSrcStudioHdr*>(data.data());
    header.id = 0;
    writeBinaryFile(path, data);

    CHECK(scaleGoldSrcMdlFile(path, 2.0f).is_error());
  }

  SECTION("rejects out of bounds section offsets")
  {
    const auto path = testDir.path() / "invalid.mdl";
    auto data = makeMinimalGoldSrcMdl();
    auto& header = *reinterpret_cast<GoldSrcStudioHdr*>(data.data());
    header.numhitboxes = 1;
    header.hitboxindex = int32_t(data.size());
    writeBinaryFile(path, data);

    CHECK(scaleGoldSrcMdlFile(path, 2.0f).is_error());
  }

  SECTION("rejects negative section counts")
  {
    const auto path = testDir.path() / "invalid.mdl";
    auto data = makeMinimalGoldSrcMdl();
    auto& header = *reinterpret_cast<GoldSrcStudioHdr*>(data.data());
    header.numhitboxes = -1;
    writeBinaryFile(path, data);

    CHECK(scaleGoldSrcMdlFile(path, 2.0f).is_error());
    CHECK(readBinaryFile(path) == data);
  }

  SECTION("rejects an invalid declared length")
  {
    const auto path = testDir.path() / "invalid.mdl";
    auto data = makeMinimalGoldSrcMdl();
    auto& header = *reinterpret_cast<GoldSrcStudioHdr*>(data.data());
    header.length = int32_t(data.size() + 1);
    writeBinaryFile(path, data);

    CHECK(scaleGoldSrcMdlFile(path, 2.0f).is_error());
    CHECK(readBinaryFile(path) == data);

    header.length = int32_t(sizeof(GoldSrcStudioHdr) - 1);
    writeBinaryFile(path, data);
    CHECK(scaleGoldSrcMdlFile(path, 2.0f).is_error());
    CHECK(readBinaryFile(path) == data);
  }
}

} // namespace tb::io
