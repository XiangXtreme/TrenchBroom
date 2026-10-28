/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

// Catalog value validation shared by the project file and the asset library index.
//
// Both containers hand untrusted strings back to the editor, which materializes them
// as cache paths or publishes them as engine paths. Validating once here keeps the
// project cache and the library working directory on the same rules.

#include "Sqlite.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace tb::project::detail
{

inline void validateText(const std::string& value, const char* field)
{
  if (value.empty() || value.find('\0') != std::string::npos)
  {
    throw StoreError{fmt::format("Invalid empty or NUL-containing {}.", field)};
  }
}

inline void validatePath(const std::string& path, const bool folder = false)
{
  if (folder && path.empty())
    return;
  validateText(path, "virtual path");
  if (
    path.front() == '/' || path.back() == '/'
    || path.find_first_of("\\:<>\"|?*") != std::string::npos
    || std::ranges::any_of(path, [](const unsigned char c) { return c < 32; }))
  {
    throw StoreError{"Virtual paths must be relative, with forward slash separators."};
  }
  size_t start = 0;
  while (start < path.size())
  {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string::npos ? end : end - start);
    if (
      part.empty() || part == "." || part == ".." || part.back() == '.'
      || part.back() == ' ')
    {
      throw StoreError{"Virtual paths cannot contain empty, dot or parent components."};
    }
    auto stem = part.substr(0, part.find('.'));
    std::ranges::transform(
      stem, stem.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
    if (
      stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
      || (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
      throw StoreError{"Virtual paths cannot contain reserved device names."};
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
}

// Catalog IDs are either generated UUIDs or deterministic ID-shaped digests.
inline bool validAssetId(const std::string& id)
{
  return id.size() == 36 && std::ranges::none_of(id, [](const unsigned char c) {
           return !std::isxdigit(c) && c != '-';
         });
}

inline void validateAssetId(const std::string& id)
{
  if (!validAssetId(id))
    throw StoreError{"Invalid catalog asset ID."};
}

} // namespace tb::project::detail
