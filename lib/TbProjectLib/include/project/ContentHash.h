/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tb::project
{

/**
 * Stable content identities.
 *
 * Asset libraries and project snapshots use SHA-256 over the asset payload as their
 * content identity. Hashing is content addressed: equal bytes always produce equal
 * digests, so a rename, move or re-import of unchanged bytes keeps the digest stable
 * while a single changed texel produces a new one. The digest is never used to decide
 * identity (IDs are), only to detect change and to compare revisions.
 */
std::string contentHash(const std::vector<std::uint8_t>& bytes);
std::string contentHash(std::string_view bytes);

/**
 * Deterministic, ID-shaped digest for a natural key.
 *
 * Library assets are enumerated from files that can be rescanned at any time. The
 * asset ID has to survive a rescan so that a project snapshot can still be matched to
 * its library origin after the library was moved or rebuilt. Deriving the ID from
 * (library ID, container, member) keeps it stable without storing a lookup table,
 * while the library ID in the key prevents two libraries from minting the same ID.
 */
std::string stableId(std::string_view key);

} // namespace tb::project
