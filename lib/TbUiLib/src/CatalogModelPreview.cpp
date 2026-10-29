/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/CatalogModelPreview.h"

#include "kd/contracts.h"

#include <utility>
namespace tb::ui
{
namespace
{
std::filesystem::path previewRoot(
  const std::filesystem::path& file, const std::filesystem::path& logicalPath)
{
  const auto path = logicalPath.empty() ? file.filename() : logicalPath;
  contract_pre(file.is_absolute());
  contract_pre(!path.has_root_path());
  auto root = file;
  for (const auto& part : path)
  {
    contract_pre(part != "..");
    root = root.parent_path();
  }
  contract_pre((root / path).lexically_normal() == file.lexically_normal());
  return root;
}
} // namespace

CatalogModelPreview::CatalogModelPreview(
  const std::filesystem::path& file,
  const mdl::GameInfo& game,
  mdl::CreateEntityModelDataResource createResource,
  Logger& logger,
  const std::filesystem::path& modelPath)
  : m_files{previewRoot(file, modelPath)}
  , m_models{game, m_files, std::move(createResource), logger}
  , m_spec{modelPath.empty() ? file.filename() : modelPath, 0, 0}
{
}
mdl::EntityModelManager& CatalogModelPreview::models()
{
  return m_models;
}
const mdl::ModelSpecification& CatalogModelPreview::specification() const
{
  return m_spec;
}
} // namespace tb::ui
