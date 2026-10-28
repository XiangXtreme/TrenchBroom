/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include "project/ContentHash.h"

#include <array>
#include <cstring>

namespace tb::project
{
namespace
{

// FIPS 180-4 SHA-256. The project library deliberately keeps hashing self contained
// instead of pulling a crypto dependency: only collision resistance against accidental
// change is needed, not message authentication.
class Sha256
{
private:
  std::array<std::uint32_t, 8> m_state{
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u};
  std::array<std::uint8_t, 64> m_block{};
  std::uint64_t m_total = 0;
  std::size_t m_used = 0;

  static std::uint32_t rotateRight(const std::uint32_t value, const int count)
  {
    return (value >> count) | (value << (32 - count));
  }

  void compress()
  {
    static constexpr std::array<std::uint32_t, 64> K{
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
      0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
      0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
      0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    auto w = std::array<std::uint32_t, 64>{};
    for (std::size_t i = 0; i < 16; ++i)
    {
      w[i] =
        (std::uint32_t{m_block[i * 4]} << 24) | (std::uint32_t{m_block[i * 4 + 1]} << 16)
        | (std::uint32_t{m_block[i * 4 + 2]} << 8) | std::uint32_t{m_block[i * 4 + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i)
    {
      const auto s0 =
        rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 =
        rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    auto a = m_state[0];
    auto b = m_state[1];
    auto c = m_state[2];
    auto d = m_state[3];
    auto e = m_state[4];
    auto f = m_state[5];
    auto g = m_state[6];
    auto h = m_state[7];
    for (std::size_t i = 0; i < 64; ++i)
    {
      const auto s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const auto choice = (e & f) ^ (~e & g);
      const auto temp1 = h + s1 + choice + K[i] + w[i];
      const auto s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
  }

public:
  void update(const std::uint8_t* data, std::size_t size)
  {
    m_total += size;
    while (size > 0)
    {
      const auto take = std::min(size, m_block.size() - m_used);
      std::memcpy(m_block.data() + m_used, data, take);
      m_used += take;
      data += take;
      size -= take;
      if (m_used == m_block.size())
      {
        compress();
        m_used = 0;
      }
    }
  }

  std::string hex()
  {
    const auto total = m_total * 8;
    auto padding = std::array<std::uint8_t, 72>{};
    auto paddingSize = std::size_t{1};
    padding[0] = 0x80;
    while ((m_used + paddingSize) % 64 != 56)
      ++paddingSize;
    for (std::size_t i = 0; i < 8; ++i)
      padding[paddingSize + i] = std::uint8_t((total >> (56 - i * 8)) & 0xff);
    paddingSize += 8;
    update(padding.data(), paddingSize);

    static constexpr char Digits[] = "0123456789abcdef";
    auto result = std::string{};
    result.reserve(64);
    for (const auto word : m_state)
    {
      for (int shift = 28; shift >= 0; shift -= 4)
        result.push_back(Digits[(word >> shift) & 0xfu]);
    }
    return result;
  }
};

std::string digestHex(const std::uint8_t* data, const std::size_t size)
{
  auto hash = Sha256{};
  if (size > 0)
    hash.update(data, size);
  return hash.hex();
}

} // namespace

std::string contentHash(const std::vector<std::uint8_t>& bytes)
{
  return digestHex(bytes.data(), bytes.size());
}

std::string contentHash(const std::string_view bytes)
{
  return digestHex(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::string stableId(const std::string_view key)
{
  const auto hex =
    digestHex(reinterpret_cast<const std::uint8_t*>(key.data()), key.size());
  // Shape the digest like a UUID so catalog IDs are interchangeable everywhere an
  // asset ID is accepted, without reusing a real UUID variant.
  auto result = std::string{};
  result.reserve(36);
  result.append(hex, 0, 8);
  result.push_back('-');
  result.append(hex, 8, 4);
  result.push_back('-');
  result.append(hex, 12, 4);
  result.push_back('-');
  result.append(hex, 16, 4);
  result.push_back('-');
  result.append(hex, 20, 12);
  return result;
}

} // namespace tb::project
