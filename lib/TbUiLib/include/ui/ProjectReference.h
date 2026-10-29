/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "project/ProjectAsset.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tb::ui
{
class ProjectSession;

/** How a name written in a map resolves against the project snapshot. */
enum class ReferenceStatus
{
  Resolved,
  Missing,
  Ambiguous,
};

struct ReferenceMatch
{
  ReferenceStatus status = ReferenceStatus::Missing;
  std::vector<project::Asset> assets;
};

/** A project WAD the index could not read. Reported instead of silently skipped. */
struct UnreadableWad
{
  std::string assetId;
  std::string virtualPath;
  std::string message;
};

/**
 * The names a project exposes to a compiler: texture names embedded in its WADs and the
 * publish paths of every other resource.
 *
 * A map still stores names, so this index is the single place that decides whether a name
 * points at exactly one project asset. Resource auditing and editor drops both query it
 * instead of matching publish paths and WAD members on their own.
 *
 * The index is read-only and derived from the current store, so it always agrees with the
 * compiler export and never mutates the project.
 */
class ProjectReferenceIndex
{
public:
  static Result<ProjectReferenceIndex> build(const ProjectSession& session);

  ProjectReferenceIndex() = default;

  /** Resolves a texture name, case insensitively, to the WAD asset embedding it. */
  ReferenceMatch resolveTexture(const std::string& name) const;

  /** Resolves a relative game path to the project asset publishing it. */
  ReferenceMatch resolvePath(const std::string& path) const;

  /** WADs whose contents could not be indexed. */
  const std::vector<UnreadableWad>& unreadableWads() const;

private:
  std::map<std::string, std::vector<project::Asset>> m_paths;
  std::map<std::string, std::vector<project::Asset>> m_textures;
  std::vector<UnreadableWad> m_unreadableWads;
};

/**
 * Normalized form of a relative game path: forward slashes, lower case.
 *
 * Empty, absolute, drive qualified and traversal paths normalize to nothing, so a
 * reference can never escape the project or depend on the platform.
 */
std::optional<std::string> normalizeGamePath(std::string path);

/**
 * The name a map uses for a captured asset: the miptex name for textures, the publish
 * path otherwise.
 *
 * A texture asset embedding more than one miptex has no single name, so this fails
 * instead of picking one; such a WAD has to be split or addressed by its member.
 */
Result<std::string> gameName(const ProjectSession& session, const project::Asset& asset);

/** The name a capture will expose, computed before the capture is committed. */
Result<std::string> capturedGameName(const project::AssetImport& import);
} // namespace tb::ui
