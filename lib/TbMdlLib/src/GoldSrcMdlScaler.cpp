#include "mdl/GoldSrcMdlScaler.h"

#include "fs/DiskIO.h"

#include "kd/filesystem_utils.h"
#include "kd/path_utils.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace tb::io
{
namespace
{
static constexpr int32_t GoldSrcStudioId_IDST = 0x54534449;
static constexpr int32_t GoldSrcStudioVersion = 10;

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
T* ptrAt(uint8_t* base, const size_t size, const int32_t offset)
{
  if (offset < 0)
  {
    return nullptr;
  }

  const auto off = size_t(offset);
  if (off > size || size - off < sizeof(T))
  {
    return nullptr;
  }

  return reinterpret_cast<T*>(base + off);
}

template <typename T>
T* ptrAt(uint8_t* base, const size_t size, const int32_t offset, const size_t count)
{
  if (count == 0)
  {
    return nullptr;
  }

  if (count > (std::numeric_limits<size_t>::max() / sizeof(T)))
  {
    return nullptr;
  }

  if (offset < 0)
  {
    return nullptr;
  }

  const auto off = size_t(offset);
  const auto bytes = sizeof(T) * count;
  if (off > size || size - off < bytes)
  {
    return nullptr;
  }

  return reinterpret_cast<T*>(base + off);
}

void scaleVec3(float v[3], const float s)
{
  v[0] *= s;
  v[1] *= s;
  v[2] *= s;
}

Result<void> scaleGoldSrcMdlBuffer(std::vector<uint8_t>& data, const float s)
{
  if (!(s > 0.0f))
  {
    return Error{"Invalid scale"};
  }

  auto* base = data.data();
  const auto size = data.size();
  auto* header = ptrAt<GoldSrcStudioHdr>(base, size, 0);
  if (!header || header->id != GoldSrcStudioId_IDST || header->version != GoldSrcStudioVersion)
  {
    return Error{"Not a GoldSrc Studio MDL"};
  }

  scaleVec3(header->eyeposition, s);
  scaleVec3(header->min, s);
  scaleVec3(header->max, s);
  scaleVec3(header->bbmin, s);
  scaleVec3(header->bbmax, s);

  if (header->numbones > 0)
  {
    auto* bones =
      ptrAt<GoldSrcMStudioBone>(base, size, header->boneindex, size_t(header->numbones));
    if (!bones)
    {
      return Error{"GoldSrc Studio MDL bone index is out of bounds"};
    }
    for (int32_t i = 0; i < header->numbones; ++i)
    {
      bones[i].value[0] *= s;
      bones[i].value[1] *= s;
      bones[i].value[2] *= s;
    }
  }

  if (header->numhitboxes > 0)
  {
    auto* hitboxes = ptrAt<GoldSrcMStudioHitbox>(
      base, size, header->hitboxindex, size_t(header->numhitboxes));
    if (!hitboxes)
    {
      return Error{"GoldSrc Studio MDL hitbox index is out of bounds"};
    }
    for (int32_t i = 0; i < header->numhitboxes; ++i)
    {
      scaleVec3(hitboxes[i].bbmin, s);
      scaleVec3(hitboxes[i].bbmax, s);
    }
  }

  if (header->numattachments > 0)
  {
    auto* attachments = ptrAt<GoldSrcMStudioAttachment>(
      base, size, header->attachmentindex, size_t(header->numattachments));
    if (!attachments)
    {
      return Error{"GoldSrc Studio MDL attachment index is out of bounds"};
    }
    for (int32_t i = 0; i < header->numattachments; ++i)
    {
      scaleVec3(attachments[i].org, s);
    }
  }

  if (header->numbodyparts > 0)
  {
    auto* bodyparts = ptrAt<GoldSrcMStudioBodyPart>(
      base, size, header->bodypartindex, size_t(header->numbodyparts));
    if (!bodyparts)
    {
      return Error{"GoldSrc Studio MDL bodypart index is out of bounds"};
    }

    for (int32_t i = 0; i < header->numbodyparts; ++i)
    {
      const auto modelCount = bodyparts[i].nummodels;
      if (modelCount <= 0)
      {
        continue;
      }

      auto* models = ptrAt<GoldSrcMStudioModel>(
        base, size, bodyparts[i].modelindex, size_t(modelCount));
      if (!models)
      {
        return Error{"GoldSrc Studio MDL model index is out of bounds"};
      }

      for (int32_t m = 0; m < modelCount; ++m)
      {
        models[m].boundingradius *= s;
        if (models[m].numverts <= 0)
        {
          continue;
        }

        auto* verts = ptrAt<float>(
          base,
          size,
          models[m].vertindex,
          size_t(models[m].numverts) * size_t(3));
        if (!verts)
        {
          return Error{"GoldSrc Studio MDL vertex index is out of bounds"};
        }

        for (int32_t v = 0; v < models[m].numverts; ++v)
        {
          verts[v * 3 + 0] *= s;
          verts[v * 3 + 1] *= s;
          verts[v * 3 + 2] *= s;
        }
      }
    }
  }

  return Result<void>{};
}
} // namespace

Result<void> scaleGoldSrcMdlFile(const std::filesystem::path& absPath, const float s)
{
  return fs::Disk::openFile(absPath) | kdl::and_then([&](auto file) -> Result<void> {
           auto reader = file->reader().buffer();
           const auto buffered = reader.buffer();
           if (buffered.size() == 0)
           {
             return Error{"Empty file"};
           }

           auto data = std::vector<uint8_t>{};
           data.resize(buffered.size());
           std::memcpy(data.data(), buffered.begin(), buffered.size());

           return scaleGoldSrcMdlBuffer(data, s) | kdl::and_then([&]() -> Result<void> {
                    return kdl::with_ostream(
                      absPath,
                      std::ios::out | std::ios::binary | std::ios::trunc,
                      [&](auto& stream) -> Result<void> {
                        stream.write(
                          reinterpret_cast<const char*>(data.data()),
                          std::streamsize(data.size()));
                        if (!stream.good())
                        {
                          return Error{"Failed to write file"};
                        }
                        return Result<void>{};
                      });
                  });
         });
}

} // namespace tb::io

