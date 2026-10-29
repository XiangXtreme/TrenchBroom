/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once
#include "fs/DiskFileSystem.h"
#include "mdl/EntityModelManager.h"
namespace tb::ui
{
/** An isolated model cache for a materialized catalog resource. */
class CatalogModelPreview
{
  fs::DiskFileSystem m_files;
  mdl::EntityModelManager m_models;
  mdl::ModelSpecification m_spec;

public:
  CatalogModelPreview(
    const std::filesystem::path& file,
    const mdl::GameInfo& game,
    mdl::CreateEntityModelDataResource createResource,
    Logger& logger,
    const std::filesystem::path& modelPath = {});
  mdl::EntityModelManager& models();
  const mdl::ModelSpecification& specification() const;
};
} // namespace tb::ui
