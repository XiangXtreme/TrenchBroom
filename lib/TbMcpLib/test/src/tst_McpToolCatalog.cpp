/*
 Copyright (C) 2026 XiangXtreme

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

#include <QJsonArray>

#include "mcp/McpToolCatalog.h"

#include <map>

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

namespace
{

const QStringList& removedPrefabConvenienceToolNames()
{
  static const auto Names = QStringList{
    "prefabs_list",
    "prefab_create",
    "blockout_create_room",
    "blockout_create_corridor",
    "blockout_create_stairs",
    "blockout_create_ramp",
    "blockout_create_doorway",
    "blockout_create_cover",
    "blockout_create_sky_shell",
  };
  return Names;
}

} // namespace

TEST_CASE("McpToolCatalog")
{
  SECTION("keeps the compatibility catalog and default profile baseline")
  {
    const auto& catalog = defaultToolCatalog();
    auto implementedCount = size_t{0};
    for (const auto& tool : catalog)
    {
      implementedCount += tool.implemented ? 1u : 0u;
    }

    CHECK(catalog.size() == 148u);
    CHECK(implementedCount == 146u);
    CHECK(toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling).size() == 53);
  }

  SECTION("registers the Python migration entry points with bounded schemas")
  {
    const auto inspect = findToolDefinition("tb_inspect");
    const auto api = findToolDefinition("tb_api");
    const auto execute = findToolDefinition("tb_execute_python");
    const auto history = findToolDefinition("tb_history");
    const auto validate = findToolDefinition("tb_validate");
    const auto capture = findToolDefinition("tb_capture");

    REQUIRE(inspect);
    REQUIRE(api);
    REQUIRE(execute);
    REQUIRE(history);
    REQUIRE(validate);
    REQUIRE(capture);
    CHECK(inspect->requiredMode == McpMode::ReadOnly);
    CHECK(api->requiredMode == McpMode::ReadOnly);
    CHECK(execute->requiredMode == McpMode::Edit);
    CHECK(execute->mutatesDocument);
    CHECK(history->requiredMode == McpMode::ReadOnly);
    CHECK(validate->requiredMode == McpMode::ReadOnly);
    CHECK(capture->requiredMode == McpMode::ReadOnly);
    CHECK(execute->inputSchema.value("properties").toObject().contains("document"));
    CHECK(execute->inputSchema.value("properties").toObject().contains("timeoutMs"));
  }

  SECTION("all mutating tools expose path and fingerprint guards")
  {
    for (const auto& tool : defaultToolCatalog())
    {
      if (!tool.mutatesDocument)
      {
        continue;
      }
      const auto properties = tool.inputSchema.value("properties").toObject();
      INFO(tool.name.toStdString());
      CHECK(properties.value("expectedDocumentPath").isObject());
      CHECK(properties.value("expectedDocumentFingerprint").isObject());
    }
  }

  SECTION("parses and names tool profiles")
  {
    CHECK(toolProfileName(McpToolProfile::Core) == "Core");
    CHECK(toolProfileName(McpToolProfile::Modeling) == "Modeling");
    CHECK(toolProfileName(McpToolProfile::Full) == "Full");

    CHECK(parseToolProfile("Core") == McpToolProfile::Core);
    CHECK(parseToolProfile("Modeling") == McpToolProfile::Modeling);
    CHECK(parseToolProfile("modeling") == McpToolProfile::Modeling);
    CHECK(parseToolProfile("Balanced") == McpToolProfile::Modeling);
    CHECK(parseToolProfile("Full") == McpToolProfile::Full);
  }

  SECTION("publishes cost metadata only through diagnostics and search")
  {
    const auto status = findToolDefinition("tb_status");
    REQUIRE(status);
    CHECK(status->costClass == McpToolCostClass::Fast);

    const auto listed = toMcpToolJson(*status);
    CHECK_FALSE(listed.contains("costClass"));
    CHECK_FALSE(listed.contains("timeoutMs"));

    const auto diagnostic = toMcpToolDiagnosticJson(*status, McpMode::ReadOnly);
    CHECK(diagnostic.value("costClass").toString() == "Fast");
    CHECK(diagnostic.value("timeoutMs").toInt() == 10'000);

    const auto search = toolsSearchJson(
      "tb_status", {}, "summary", McpMode::ReadOnly, McpToolProfile::Core);
    REQUIRE(search.size() == 1);
    CHECK(search.at(0).toObject().value("costClass").toString() == "Fast");
    CHECK(search.at(0).toObject().value("timeoutMs").toInt() == 10'000);
  }

  SECTION("contains first-phase tools")
  {
    CHECK(findToolDefinition("tb_status"));
    CHECK(findToolDefinition("documents_list"));
    CHECK(findToolDefinition("documents_open"));
    CHECK(findToolDefinition("documents_open_verified"));
    CHECK(findToolDefinition("documents_activate"));
    CHECK(findToolDefinition("documents_save"));
    CHECK(findToolDefinition("documents_save_current"));
    CHECK(findToolDefinition("documents_save_as"));
    CHECK(findToolDefinition("documents_close"));
    CHECK(findToolDefinition("documents_export"));
    CHECK(findToolDefinition("map_snapshot"));
    CHECK(findToolDefinition("map_search"));
    CHECK(findToolDefinition("selection_get"));
    CHECK(findToolDefinition("selection_inspect"));
    CHECK(findToolDefinition("selection_filter"));
    CHECK(findToolDefinition("selection_by_bounds"));
    CHECK(findToolDefinition("selection_grow"));
    CHECK(findToolDefinition("viewport_focus"));
    CHECK(findToolDefinition("viewport_clear_marks"));
    CHECK(findToolDefinition("viewport_layout_get"));
    CHECK(findToolDefinition("viewport_layout_set"));
    CHECK(findToolDefinition("viewport_camera_frame_bounds"));
    CHECK(findToolDefinition("viewport_camera_set"));
    CHECK(findToolDefinition("viewport_capture_current"));
    CHECK(findToolDefinition("viewport_capture_3d"));
    CHECK(findToolDefinition("viewport_capture_2d"));
    CHECK(findToolDefinition("viewport_capture_scene_review"));
    CHECK(findToolDefinition("render_review_targets"));
    CHECK(findToolDefinition("render_review_current_scene"));
    CHECK(findToolDefinition("render_review_operation"));
    CHECK(findToolDefinition("selector_preview"));
    CHECK(findToolDefinition("objects_select_by_selector"));
    CHECK(findToolDefinition("objects_delete_by_selector"));
    CHECK(findToolDefinition("render_review_selector"));
    CHECK(findToolDefinition("module_list"));
    CHECK(findToolDefinition("module_inspect"));
    CHECK(findToolDefinition("module_select"));
    CHECK(findToolDefinition("module_render_review"));
    CHECK(findToolDefinition("module_validate"));
    CHECK(findToolDefinition("module_compact"));
    CHECK(findToolDefinition("fgd_entities_list"));
    CHECK(findToolDefinition("entity_schema"));
    CHECK(findToolDefinition("entity_create_from_schema"));
    CHECK(findToolDefinition("entity_create_checked"));
    CHECK(findToolDefinition("entity_create_checked_batch"));
    CHECK(findToolDefinition("entity_properties_update"));
    CHECK(findToolDefinition("entity_properties_delete"));
    CHECK(findToolDefinition("entity_tie_brushes"));
    CHECK(findToolDefinition("entity_untie_brushes"));
    CHECK(findToolDefinition("brush_types_list"));
    CHECK(findToolDefinition("brush_create"));
    CHECK(findToolDefinition("brush_create_cone"));
    CHECK(findToolDefinition("brush_create_pipe"));
    CHECK(findToolDefinition("brush_create_sphere"));
    CHECK(findToolDefinition("brush_create_pyramid"));
    CHECK(findToolDefinition("brush_create_tetrahedron"));
    CHECK(findToolDefinition("brush_create_from_planes"));
    CHECK(findToolDefinition("brush_create_prism"));
    CHECK(findToolDefinition("brush_create_cylinder_sector"));
    CHECK(findToolDefinition("brush_create_boxes_batch"));
    CHECK(findToolDefinition("brush_create_polygon_batch"));
    CHECK(findToolDefinition("shape_library_list"));
    CHECK(findToolDefinition("brush_metadata_set"));
    CHECK(findToolDefinition("brush_metadata_get"));
    CHECK(findToolDefinition("selection_by_metadata"));
    CHECK(findToolDefinition("route_geometry_analyze_chain"));
    CHECK(findToolDefinition("kz_distance_analyze_chain"));
    CHECK(findToolDefinition("brush_create_arch"));
    CHECK(findToolDefinition("brush_create_torus"));
    CHECK(findToolDefinition("objects_delete"));
    CHECK(findToolDefinition("objects_delete_by_filter"));
    CHECK(findToolDefinition("objects_delete_by_operation"));
    CHECK(findToolDefinition("objects_transform"));
    CHECK(findToolDefinition("group_create_from_selection"));
    CHECK(findToolDefinition("group_inspect"));
    CHECK(findToolDefinition("group_rename_selected"));
    CHECK(findToolDefinition("group_ungroup_selected"));
    CHECK(findToolDefinition("map_validate"));
    CHECK(findToolDefinition("problems_check"));
    CHECK(findToolDefinition("problems_fix"));
    CHECK(findToolDefinition("map_fix_all_safe"));
    CHECK(findToolDefinition("compile_profiles_list"));
    CHECK(findToolDefinition("compile_run"));
    CHECK(findToolDefinition("compile_log_tail"));
    CHECK(findToolDefinition("leaks_load_pointfile"));
    CHECK(!findToolDefinition("prefabs_list"));
    CHECK(!findToolDefinition("prefab_create"));
    CHECK(findToolDefinition("texture_lock_get"));
    CHECK(findToolDefinition("texture_lock_set"));
    CHECK(findToolDefinition("texture_apply_by_filter"));
    CHECK(findToolDefinition("geometry_analyze_selection"));
    CHECK(findToolDefinition("geometry_analyze_slopes"));
    CHECK(findToolDefinition("geometry_analyze_route_continuity"));
    CHECK(findToolDefinition("geometry_analyze_shell_seams"));
    CHECK(findToolDefinition("blockout_create_spiral_stairs"));
    CHECK(findToolDefinition("blockout_validate_spiral_stairs"));
    CHECK(findToolDefinition("operation_inspect"));
    CHECK(findToolDefinition("operation_select"));
    CHECK(findToolDefinition("operation_validate"));
    CHECK(findToolDefinition("history_status"));
    CHECK(findToolDefinition("blockout_create_batch"));
    CHECK(findToolDefinition("blockout_create_curved_corridor"));
    CHECK(!findToolDefinition("blockout_create_room"));
    CHECK(!findToolDefinition("blockout_create_corridor"));
    CHECK(!findToolDefinition("blockout_create_stairs"));
    CHECK(!findToolDefinition("blockout_create_ramp"));
    CHECK(!findToolDefinition("blockout_create_doorway"));
    CHECK(!findToolDefinition("blockout_create_cover"));
    CHECK(!findToolDefinition("blockout_create_sky_shell"));
    CHECK(findToolDefinition("python_generate_blockout"));
    CHECK(findToolDefinition("heightmap_import_grayscale"));
    CHECK(findToolDefinition("heightmap_preview_grayscale"));
    CHECK(findToolDefinition("ir_validate"));
    CHECK(findToolDefinition("ir_compile_preview"));
    CHECK(findToolDefinition("ir_compile_preview_from_file"));
    CHECK(findToolDefinition("ir_apply"));
    CHECK(findToolDefinition("ir_apply_from_file"));
    CHECK(findToolDefinition("tb_tools_search"));
    CHECK(findToolDefinition("actions_list"));
    CHECK(findToolDefinition("overlay_set"));
  }

  SECTION("read-only mode lists implemented read-only tools only")
  {
    const auto tools = toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Full);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("documents_activate"));
    CHECK(names.contains("map_snapshot"));
    CHECK(names.contains("map_search"));
    CHECK(names.contains("selection_set"));
    CHECK(names.contains("selection_filter"));
    CHECK(names.contains("selection_by_bounds"));
    CHECK(names.contains("selection_grow"));
    CHECK(names.contains("viewport_focus"));
    CHECK(names.contains("viewport_clear_marks"));
    CHECK(names.contains("viewport_layout_get"));
    CHECK(names.contains("viewport_layout_set"));
    CHECK(names.contains("viewport_camera_frame_bounds"));
    CHECK(names.contains("viewport_camera_set"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(names.contains("viewport_capture_3d"));
    CHECK(names.contains("viewport_capture_2d"));
    CHECK(names.contains("viewport_capture_scene_review"));
    CHECK(names.contains("render_review_targets"));
    CHECK(names.contains("render_review_current_scene"));
    CHECK(names.contains("render_review_operation"));
    CHECK(names.contains("fgd_entities_list"));
    CHECK(names.contains("entity_schema"));
    CHECK(names.contains("brush_types_list"));
    CHECK(names.contains("shape_library_list"));
    CHECK(names.contains("brush_metadata_get"));
    CHECK(names.contains("selection_by_metadata"));
    CHECK(names.contains("route_geometry_analyze_chain"));
    CHECK(names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("overlay_set"));
    CHECK(names.contains("history_list"));
    CHECK(names.contains("history_status"));
    CHECK(names.contains("asset_search"));
    CHECK(names.contains("textures_list"));
    CHECK(names.contains("texture_search"));
    CHECK(names.contains("texture_lock_get"));
    CHECK(names.contains("face_list"));
    CHECK(names.contains("face_select"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));
    CHECK(names.contains("compile_profiles_list"));
    CHECK(names.contains("compile_log_tail"));
    CHECK(names.contains("blockout_validate"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("geometry_analyze_slopes"));
    CHECK(names.contains("geometry_analyze_route_continuity"));
    CHECK(names.contains("geometry_analyze_shell_seams"));
    CHECK(names.contains("blockout_validate_spiral_stairs"));
    CHECK(!names.contains("documents_open"));
    CHECK(!names.contains("documents_save"));
    CHECK(!names.contains("documents_close"));
    CHECK(!names.contains("documents_export"));
    CHECK(!names.contains("entity_create_from_schema"));
    CHECK(!names.contains("entity_tie_brushes"));
    CHECK(!names.contains("entity_untie_brushes"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("brush_create_box"));
    CHECK(!names.contains("brush_create_prism"));
    CHECK(!names.contains("brush_create_cylinder_sector"));
    CHECK(!names.contains("brush_create_polygon_batch"));
    CHECK(!names.contains("brush_metadata_set"));
    CHECK(!names.contains("blockout_create_spiral_stairs"));
    CHECK(!names.contains("asset_place_model"));
    CHECK(!names.contains("prefabs_list"));
    CHECK(!names.contains("prefab_create"));
    CHECK(!names.contains("texture_apply"));
    CHECK(!names.contains("texture_lock_set"));
    CHECK(!names.contains("texture_replace"));
    CHECK(!names.contains("face_texture_set"));
    CHECK(!names.contains("objects_delete"));
    CHECK(!names.contains("objects_transform"));
    CHECK(!names.contains("problems_fix"));
    CHECK(!names.contains("map_fix_all_safe"));
    CHECK(!names.contains("compile_run"));
    CHECK(!names.contains("leaks_load_pointfile"));
    CHECK(!names.contains("blockout_create_room"));
    CHECK(!names.contains("python_generate_blockout"));
    CHECK(!names.contains("action_execute"));
    CHECK(!names.contains("history_undo_mcp"));
  }

  SECTION("modeling profile is the default and keeps modeling tools visible")
  {
    const auto tools = toolsListJson(McpMode::Edit);
    const auto explicitTools =
      toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    CHECK(tools == explicitTools);

    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }
    names.sort();

    INFO("Modeling profile tools: " << names.join(", ").toStdString());
    CHECK(
      names.size()
      <= 53); // save-current/as, documents_list, and entity link inspection are visible.

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("tb_doctor"));
    CHECK(names.contains("tb_tools_search"));
    CHECK(names.contains("selection_inspect"));
    CHECK(names.contains("entity_link_chain_inspect"));
    CHECK(!names.contains("selection_get"));
    CHECK(!names.contains("selection_set"));
    CHECK(!names.contains("selection_filter"));
    CHECK(!names.contains("selection_by_bounds"));
    CHECK(!names.contains("selection_grow"));
    CHECK(names.contains("map_snapshot"));
    CHECK(!names.contains("map_search"));
    CHECK(!names.contains("fgd_entities_list"));
    CHECK(!names.contains("entity_schema"));
    CHECK(names.contains("entity_create_checked"));
    CHECK(names.contains("entity_create_checked_batch"));
    CHECK(!names.contains("entity_create_from_schema"));
    CHECK(!names.contains("entity_tie_brushes"));
    CHECK(!names.contains("entity_untie_brushes"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("entity_update"));
    CHECK(!names.contains("entity_delete"));
    CHECK(names.contains("operation_inspect"));
    CHECK(!names.contains("operation_select"));
    CHECK(names.contains("operation_validate"));
    CHECK(!names.contains("history_list"));
    CHECK(names.contains("history_undo_mcp"));
    CHECK(names.contains("history_undo_to_operation"));
    CHECK(names.contains("history_redo_mcp"));
    CHECK(names.contains("selector_preview"));
    CHECK(names.contains("objects_select_by_selector"));
    CHECK(names.contains("objects_delete_by_selector"));
    CHECK(names.contains("render_review_selector"));
    CHECK(names.contains("module_list"));
    CHECK(names.contains("module_inspect"));
    CHECK(!names.contains("module_select"));
    CHECK(names.contains("module_render_review"));
    CHECK(names.contains("module_validate"));
    CHECK(!names.contains("brush_types_list"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("brush_create_box"));
    CHECK(!names.contains("brush_create_wedge"));
    CHECK(!names.contains("brush_create_cylinder"));
    CHECK(!names.contains("brush_create_cone"));
    CHECK(!names.contains("brush_create_pipe"));
    CHECK(!names.contains("brush_create_sphere"));
    CHECK(!names.contains("brush_create_pyramid"));
    CHECK(!names.contains("brush_create_tetrahedron"));
    CHECK(!names.contains("brush_create_prism"));
    CHECK(!names.contains("brush_create_cylinder_sector"));
    CHECK(names.contains("brush_create_boxes_batch"));
    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(!names.contains("brush_create_from_planes"));
    CHECK(!names.contains("shape_library_list"));
    CHECK(!names.contains("brush_metadata_set"));
    CHECK(!names.contains("brush_metadata_get"));
    CHECK(!names.contains("selection_by_metadata"));
    CHECK(!names.contains("route_geometry_analyze_chain"));
    CHECK(!names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("blockout_create_batch"));
    CHECK(!names.contains("python_generate_blockout"));
    CHECK(names.contains("heightmap_import_grayscale"));
    CHECK(names.contains("heightmap_preview_grayscale"));
    CHECK(!names.contains("ir_validate"));
    CHECK(names.contains("ir_compile_preview"));
    CHECK(names.contains("ir_apply"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("geometry_analyze_shell_seams"));
    CHECK(names.contains("geometry_csg_selection"));
    CHECK(!names.contains("blockout_validate"));
    CHECK(!names.contains("objects_delete"));
    CHECK(!names.contains("objects_delete_by_filter"));
    CHECK(!names.contains("objects_delete_by_operation"));
    CHECK(names.contains("objects_transform"));
    CHECK(names.contains("group_create_from_selection"));
    CHECK(names.contains("group_inspect"));
    CHECK(!names.contains("group_rename_selected"));
    CHECK(!names.contains("group_ungroup_selected"));
    CHECK(!names.contains("textures_list"));
    CHECK(names.contains("texture_search"));
    CHECK(!names.contains("texture_lock_get"));
    CHECK(!names.contains("texture_lock_set"));
    CHECK(!names.contains("texture_apply"));
    CHECK(names.contains("texture_apply_by_filter"));
    CHECK(!names.contains("texture_replace"));
    CHECK(names.contains("texture_align_face"));
    CHECK(!names.contains("texture_copy_from_face"));
    CHECK(!names.contains("face_list"));
    CHECK(!names.contains("face_select"));
    CHECK(!names.contains("face_texture_set"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));

    CHECK(!names.contains("actions_list"));
    CHECK(!names.contains("action_execute"));
    CHECK(!names.contains("asset_search"));
    CHECK(!names.contains("asset_place_model"));

    const auto textureListSearch = toolsSearchJson(
      "textures_list", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(textureListSearch.size() == 1);
    CHECK(
      textureListSearch.first().toObject().value("name").toString() == "textures_list");
    CHECK_FALSE(
      textureListSearch.first().toObject().value("visibleInCurrentProfile").toBool());
    const auto textureCopySearch = toolsSearchJson(
      "texture_copy_from_face", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(textureCopySearch.size() == 1);
    CHECK_FALSE(
      textureCopySearch.first().toObject().value("visibleInCurrentProfile").toBool());
    CHECK(!names.contains("asset_place_sprite"));
    CHECK(!names.contains("asset_place_sound"));
    CHECK(!names.contains("blockout_create_room"));
    CHECK(!names.contains("blockout_create_corridor"));
    CHECK(!names.contains("blockout_create_stairs"));
    CHECK(!names.contains("blockout_create_ramp"));
    CHECK(!names.contains("blockout_create_doorway"));
    CHECK(!names.contains("blockout_create_cover"));
    CHECK(!names.contains("blockout_create_sky_shell"));
    CHECK(!names.contains("blockout_create_spiral_stairs"));
    CHECK(!names.contains("blockout_create_curved_corridor"));
    CHECK(!names.contains("blockout_validate_spiral_stairs"));
    CHECK(!names.contains("documents_open"));
    CHECK(names.contains("documents_list"));
    CHECK(names.contains("documents_open_verified"));
    CHECK(!names.contains("documents_save"));
    CHECK(names.contains("documents_save_current"));
    CHECK(names.contains("documents_save_as"));
    CHECK(!names.contains("documents_close"));
    CHECK(!names.contains("documents_export"));
    CHECK(!names.contains("compile_profiles_list"));
    CHECK(!names.contains("compile_run"));
    CHECK(!names.contains("compile_log_tail"));
    CHECK(!names.contains("leaks_load_pointfile"));
    CHECK(!names.contains("problems_fix"));
    CHECK(!names.contains("map_fix_all_safe"));
    CHECK(!names.contains("viewport_focus"));
    CHECK(!names.contains("viewport_clear_marks"));
    CHECK(!names.contains("viewport_layout_get"));
    CHECK(!names.contains("viewport_layout_set"));
    CHECK(!names.contains("viewport_camera_frame_bounds"));
    CHECK(!names.contains("viewport_camera_set"));
    CHECK(!names.contains("viewport_capture_3d"));
    CHECK(!names.contains("viewport_capture_scene_review"));
    CHECK(!names.contains("render_review_targets"));
    CHECK(names.contains("render_review_current_scene"));
    CHECK(names.contains("render_review_operation"));
    CHECK(!names.contains("overlay_set"));
    CHECK(!names.contains("overlay_clear"));
  }

  SECTION("group tools expose native grouping path without profile noise")
  {
    const auto createTool = findToolDefinition("group_create_from_selection");
    const auto inspectTool = findToolDefinition("group_inspect");
    const auto renameTool = findToolDefinition("group_rename_selected");
    const auto ungroupTool = findToolDefinition("group_ungroup_selected");
    REQUIRE(createTool);
    REQUIRE(inspectTool);
    REQUIRE(renameTool);
    REQUIRE(ungroupTool);

    CHECK(createTool->category == "group");
    CHECK(inspectTool->category == "group");
    CHECK(renameTool->category == "group");
    CHECK(ungroupTool->category == "group");
    CHECK(createTool->mutatesDocument);
    CHECK_FALSE(inspectTool->mutatesDocument);
    CHECK(renameTool->mutatesDocument);
    CHECK(ungroupTool->mutatesDocument);

    const auto modelingTools =
      toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto modelingNames = QStringList{};
    for (const auto& entry : modelingTools)
    {
      modelingNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(modelingNames.contains("group_create_from_selection"));
    CHECK(modelingNames.contains("group_inspect"));
    CHECK_FALSE(modelingNames.contains("group_rename_selected"));
    CHECK_FALSE(modelingNames.contains("group_ungroup_selected"));

    const auto searchResults =
      toolsSearchJson("group", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto searchNames = QStringList{};
    auto visibility = std::map<QString, bool>{};
    for (const auto& entry : searchResults)
    {
      const auto object = entry.toObject();
      const auto name = object.value("name").toString();
      searchNames.push_back(name);
      visibility[name] = object.value("visibleInCurrentProfile").toBool();
      CHECK_FALSE(object.value("inputSchema").isObject());
      CHECK(object.value("schemaAvailable").toBool());
      CHECK(object.value("schemaOmittedReason").toString() == "multiple_matches");
      CHECK(object.value("schemaQuery").toString() == name);
    }
    CHECK(searchNames.contains("group_create_from_selection"));
    CHECK(searchNames.contains("group_inspect"));
    CHECK(searchNames.contains("group_rename_selected"));
    CHECK(searchNames.contains("group_ungroup_selected"));
    CHECK(visibility["group_create_from_selection"]);
    CHECK(visibility["group_inspect"]);
    CHECK_FALSE(visibility["group_rename_selected"]);
    CHECK_FALSE(visibility["group_ungroup_selected"]);

    const auto exactSearchResults = toolsSearchJson(
      "group_create_from_selection",
      "",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    REQUIRE(exactSearchResults.size() == 1);
    CHECK(exactSearchResults.first().toObject().value("inputSchema").isObject());
  }

  SECTION("modeling read-only profile hides entity schema helpers by default")
  {
    const auto tools = toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(!names.contains("fgd_entities_list"));
    CHECK(!names.contains("entity_schema"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("entity_create_from_schema"));
    CHECK(!names.contains("entity_create_checked_batch"));

    const auto searchResults = toolsSearchJson(
      "entity_schema", "", "schema", McpMode::ReadOnly, McpToolProfile::Modeling);
    REQUIRE(searchResults.size() == 1);
    const auto found = searchResults.first().toObject();
    CHECK(found.value("name").toString() == "entity_schema");
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("heightmap import tool requires image path and is modeling visible")
  {
    const auto tool = findToolDefinition("heightmap_import_grayscale");
    REQUIRE(tool);

    CHECK(tool->category == "heightmap");
    CHECK(tool->requiredMode == McpMode::Edit);
    CHECK(tool->mutatesDocument);

    const auto required = tool->inputSchema.value("required").toArray();
    CHECK(required.contains("imagePath"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.contains("mode"));
    CHECK(properties.contains("minCellSize"));
    CHECK(properties.contains("maxCellSize"));
    CHECK(properties.contains("errorTolerance"));

    const auto readOnlyTools =
      toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Modeling);
    auto readOnlyNames = QStringList{};
    for (const auto& entry : readOnlyTools)
    {
      readOnlyNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(!readOnlyNames.contains("heightmap_import_grayscale"));

    const auto editTools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto editNames = QStringList{};
    for (const auto& entry : editTools)
    {
      editNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(editNames.contains("heightmap_import_grayscale"));
  }

  SECTION("state trust tools expose compact schemas and modeling visibility")
  {
    const auto verifiedOpen = findToolDefinition("documents_open_verified");
    REQUIRE(verifiedOpen);
    CHECK(verifiedOpen->requiredMode == McpMode::Edit);
    CHECK(verifiedOpen->mutatesDocument);
    CHECK(verifiedOpen->inputSchema.value("required").toArray().contains("path"));
    CHECK(verifiedOpen->inputSchema.value("properties")
            .toObject()
            .contains("expectedDocumentPath"));

    const auto historyStatus = findToolDefinition("history_status");
    REQUIRE(historyStatus);
    CHECK(historyStatus->requiredMode == McpMode::ReadOnly);
    CHECK(!historyStatus->mutatesDocument);

    const auto heightmapPreview = findToolDefinition("heightmap_preview_grayscale");
    REQUIRE(heightmapPreview);
    CHECK(heightmapPreview->category == "heightmap");
    CHECK(heightmapPreview->requiredMode == McpMode::ReadOnly);
    CHECK(!heightmapPreview->mutatesDocument);
    CHECK(
      heightmapPreview->inputSchema.value("required").toArray().contains("imagePath"));

    const auto editTools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto editNames = QStringList{};
    for (const auto& entry : editTools)
    {
      editNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(editNames.contains("history_status"));
    CHECK(editNames.contains("heightmap_preview_grayscale"));
    CHECK(editNames.contains("documents_open_verified"));
    CHECK(!editNames.contains("documents_open"));

    const auto searchResults = toolsSearchJson(
      "documents_open_verified", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(searchResults.size() == 1);
    const auto found = searchResults.first().toObject();
    CHECK(found.value("name").toString() == "documents_open_verified");
    CHECK(found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("CSG selection tool exposes compact selection-only schema")
  {
    const auto tool = findToolDefinition("geometry_csg_selection");
    REQUIRE(tool);
    CHECK(tool->requiredMode == McpMode::Edit);
    CHECK(tool->mutatesDocument);
    CHECK(tool->category == "geometry");
    CHECK(tool->description.contains("selection-only"));

    const auto schema = tool->inputSchema;
    CHECK(schema.value("required").toArray().contains("operation"));
    const auto properties = schema.value("properties").toObject();
    CHECK(properties.value("operation")
            .toObject()
            .value("description")
            .toString()
            .contains("convex_merge"));
    CHECK(properties.value("operation")
            .toObject()
            .value("description")
            .toString()
            .contains("subtract"));
    CHECK(properties.value("operation")
            .toObject()
            .value("description")
            .toString()
            .contains("intersect"));
    CHECK(properties.value("operation")
            .toObject()
            .value("description")
            .toString()
            .contains("hollow"));
    CHECK(properties.value("idsMode")
            .toObject()
            .value("description")
            .toString()
            .contains("sample"));
    CHECK(properties.value("expectedDocumentPath").isObject());
  }

  SECTION("core profile keeps only compact discovery and batch-oriented tools")
  {
    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Core);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("tb_doctor"));
    CHECK(names.contains("tb_tools_search"));
    CHECK(names.contains("blockout_create_batch"));
    CHECK(names.contains("operation_inspect"));
    CHECK(names.contains("operation_select"));
    CHECK(names.contains("operation_validate"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("blockout_validate"));
    CHECK(!names.contains("geometry_csg_selection"));
    CHECK(!names.contains("python_generate_blockout"));
    CHECK(!names.contains("documents_open"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("blockout_create_room"));
  }

  SECTION("full profile exposes implemented expert brush tools")
  {
    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Full);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("brush_create"));
    CHECK(names.contains("brush_create_box"));
    CHECK(names.contains("brush_create_wedge"));
    CHECK(names.contains("brush_create_cylinder"));
    CHECK(names.contains("brush_create_cone"));
    CHECK(names.contains("brush_create_pipe"));
    CHECK(names.contains("brush_create_sphere"));
    CHECK(names.contains("brush_create_pyramid"));
    CHECK(names.contains("brush_create_tetrahedron"));
    CHECK(names.contains("brush_create_from_planes"));
    CHECK(names.contains("brush_create_prism"));
    CHECK(names.contains("brush_create_cylinder_sector"));
    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(names.contains("geometry_csg_selection"));
    CHECK(!names.contains("brush_create_arch"));
    CHECK(!names.contains("brush_create_torus"));
  }

  SECTION("python blockout tool requires script and is discoverable from core profile")
  {
    const auto tool = findToolDefinition("python_generate_blockout");

    REQUIRE(tool);
    CHECK(tool->category == "python");
    CHECK(tool->requiredMode == McpMode::Edit);
    CHECK(tool->mutatesDocument);

    const auto required = tool->inputSchema.value("required").toArray();
    CHECK(required.contains("script"));

    const auto tools = toolsSearchJson(
      "python_generate_blockout", "", "schema", McpMode::Edit, McpToolProfile::Core);
    auto found = QJsonObject{};
    for (const auto& entry : tools)
    {
      const auto object = entry.toObject();
      if (object.value("name").toString() == "python_generate_blockout")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "python");
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("curved corridor exposes snap mode schema")
  {
    const auto tool = findToolDefinition("blockout_create_curved_corridor");
    REQUIRE(tool);

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("turnDegrees")
            .toObject()
            .value("description")
            .toString()
            .contains("360"));
    CHECK(properties.value("slopeStartZ")
            .toObject()
            .value("description")
            .toString()
            .contains("terraced"));
    CHECK(properties.value("wallThickness")
            .toObject()
            .value("description")
            .toString()
            .contains("inner_wall"));
    CHECK(properties.value("floorThickness")
            .toObject()
            .value("description")
            .toString()
            .contains("floor"));
    CHECK(properties.value("ceilingThickness")
            .toObject()
            .value("description")
            .toString()
            .contains("ceiling"));
    CHECK(properties.value("snapMode").isObject());
    CHECK(properties.value("snapMode")
            .toObject()
            .value("description")
            .toString()
            .contains("radial"));
    CHECK(properties.value("grid").isObject());
    CHECK(properties.value("defaultMetadata").isObject());
    CHECK(properties.value("metadata").isObject());
    CHECK(properties.value("parts").isObject());
    CHECK(properties.value("partMaterials").isObject());
    CHECK(properties.value("partMetadata").isObject());

    const auto sectorTool = findToolDefinition("brush_create_cylinder_sector");
    REQUIRE(sectorTool);
    CHECK(sectorTool->inputSchema.value("properties")
            .toObject()
            .value("snapMode")
            .isObject());
  }

  SECTION("tool search can discover hidden tools from modeling profile")
  {
    for (const auto* hiddenToolName :
         {"viewport_capture_3d",
          "render_review_targets",
          "selection_by_metadata",
          "face_list",
          "map_search",
          "history_list",
          "operation_select",
          "module_select",
          "ir_validate"})
    {
      CAPTURE(hiddenToolName);
      const auto tools = toolsSearchJson(
        hiddenToolName, "", "schema", McpMode::Edit, McpToolProfile::Modeling);
      auto found = QJsonObject{};
      for (const auto& tool : tools)
      {
        const auto object = tool.toObject();
        if (object.value("name").toString() == hiddenToolName)
        {
          found = object;
          break;
        }
      }

      REQUIRE(!found.isEmpty());
      CHECK(!found.value("visibleInCurrentProfile").toBool());
      CHECK(found.value("inputSchema").isObject());
    }
  }

  SECTION("selector module and IR tools expose compact atomic workflow schemas")
  {
    const auto selectorTool = findToolDefinition("selector_preview");
    REQUIRE(selectorTool);
    CHECK(selectorTool->requiredMode == McpMode::ReadOnly);
    CHECK(!selectorTool->mutatesDocument);

    const auto selectorProperties =
      selectorTool->inputSchema.value("properties").toObject();
    CHECK(selectorProperties.value("selector").isObject());
    CHECK(selectorProperties.value("idsMode").isObject());
    CHECK(selectorProperties.value("detail").isObject());

    const auto selectorSchema =
      selectorProperties.value("selector").toObject().value("properties").toObject();
    CHECK(selectorSchema.value("metadata").isObject());
    CHECK(selectorSchema.value("moduleId").isObject());
    CHECK(selectorSchema.value("operationIds").isObject());
    CHECK(selectorSchema.value("boundsMode").isObject());

    const auto renderSelectorTool = findToolDefinition("render_review_selector");
    REQUIRE(renderSelectorTool);
    CHECK(renderSelectorTool->requiredMode == McpMode::ReadOnly);
    CHECK(renderSelectorTool->inputSchema.value("properties")
            .toObject()
            .value("contactSheetMaxCaptures")
            .toObject()
            .value("description")
            .toString()
            .contains("Defaults to 2"));

    const auto moduleValidate = findToolDefinition("module_validate");
    REQUIRE(moduleValidate);
    CHECK(moduleValidate->requiredMode == McpMode::ReadOnly);
    CHECK(moduleValidate->inputSchema.value("required").toArray().contains("moduleId"));
    CHECK(moduleValidate->inputSchema.value("properties")
            .toObject()
            .value("checkRouteContinuity")
            .isObject());
    CHECK(moduleValidate->inputSchema.value("properties")
            .toObject()
            .value("orderBy")
            .isObject());
    CHECK(moduleValidate->inputSchema.value("properties")
            .toObject()
            .value("closedLoop")
            .isObject());
    CHECK(moduleValidate->inputSchema.value("properties")
            .toObject()
            .value("qualityPolicy")
            .isObject());

    const auto moduleCompact = findToolDefinition("module_compact");
    REQUIRE(moduleCompact);
    CHECK(moduleCompact->inputSchema.value("required").toArray().contains("moduleId"));

    const auto irApply = findToolDefinition("ir_apply");
    REQUIRE(irApply);
    CHECK(irApply->requiredMode == McpMode::Edit);
    CHECK(irApply->mutatesDocument);
    const auto irProperties = irApply->inputSchema.value("properties").toObject();
    CHECK(irProperties.value("ir").isObject());
    CHECK(irProperties.value("operations").isObject());
    CHECK(irProperties.value("entities").isObject());
    CHECK(irProperties.value("moduleId").isObject());
    CHECK(irProperties.value("defaultMetadata").isObject());
    CHECK(irProperties.value("qualityPolicy").isObject());
    CHECK(irProperties.value("requireMaterialAvailable").isObject());
    CHECK(irProperties.value("applyMode").isObject());
    CHECK(irProperties.value("expectedIrHash").isObject());
    CHECK(irProperties.value("expectedTargetModuleRevision").isObject());
    CHECK(irProperties.value("expectedTargetModuleContentHash").isObject());
    CHECK(irProperties.value("expectedTargetCanonicalObjectIds").isObject());

    const auto irPreview = findToolDefinition("ir_compile_preview");
    REQUIRE(irPreview);
    const auto irPreviewProperties =
      irPreview->inputSchema.value("properties").toObject();
    CHECK(irPreviewProperties.value("applyMode").isObject());
    CHECK(irPreviewProperties.value("moduleId").isObject());
    CHECK(irPreviewProperties.value("defaultMetadata").isObject());
    CHECK(irPreviewProperties.value("requireMaterialAvailable").isObject());

    const auto irPreviewFile = findToolDefinition("ir_compile_preview_from_file");
    REQUIRE(irPreviewFile);
    CHECK(irPreviewFile->requiredMode == McpMode::ReadOnly);
    CHECK(irPreviewFile->description.contains("previewId"));
    CHECK(irPreviewFile->inputSchema.value("required").toArray().contains("path"));
    CHECK(irPreviewFile->inputSchema.value("properties")
            .toObject()
            .value("qualityPolicy")
            .isObject());
    CHECK(irPreviewFile->inputSchema.value("properties")
            .toObject()
            .value("applyMode")
            .isObject());
    CHECK(irPreviewFile->inputSchema.value("properties")
            .toObject()
            .value("requireMaterialAvailable")
            .isObject());

    const auto irApplyFile = findToolDefinition("ir_apply_from_file");
    REQUIRE(irApplyFile);
    CHECK(irApplyFile->requiredMode == McpMode::Edit);
    CHECK(irApplyFile->mutatesDocument);
    CHECK(irApplyFile->description.contains("previewId"));
    const auto irApplyFileProperties =
      irApplyFile->inputSchema.value("properties").toObject();
    CHECK(irApplyFileProperties.value("path").isObject());
    CHECK(irApplyFileProperties.value("previewId").isObject());
    CHECK(irApplyFileProperties.value("qualityPolicy").isObject());
    CHECK(irApplyFileProperties.value("applyMode").isObject());
    CHECK(irApplyFileProperties.value("requireMaterialAvailable").isObject());

    const auto blockoutBatch = findToolDefinition("blockout_create_batch");
    REQUIRE(blockoutBatch);
    const auto batchProperties =
      blockoutBatch->inputSchema.value("properties").toObject();
    CHECK(batchProperties.value("defaultMetadata").isObject());
    CHECK(batchProperties.value("qualityPolicy").isObject());
    CHECK(batchProperties.value("material").isObject());
    CHECK(batchProperties.value("requireMaterialAvailable").isObject());

    const auto brushCreate = findToolDefinition("brush_create");
    REQUIRE(brushCreate);
    CHECK(brushCreate->inputSchema.value("properties")
            .toObject()
            .value("requireMaterialAvailable")
            .isObject());

    const auto operationsProperty = batchProperties.value("operations").toObject();
    CHECK(operationsProperty.value("description").toString().contains("skill recipes"));
    CHECK(operationsProperty.value("description").toString().contains("explicit"));

    const auto operationItemSchema = operationsProperty.value("items").toObject();
    const auto operationItemDescription =
      operationItemSchema.value("description").toString();
    CHECK(operationItemDescription.contains("Primitive examples"));
    CHECK(operationItemDescription.contains("skill recipes"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"room")"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"corridor")"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"stairs")"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"doorway")"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"cover")"));
    CHECK_FALSE(operationItemDescription.contains(R"("type":"sky_shell")"));
    const auto operationItems = operationItemSchema.value("properties").toObject();
    const auto typeDescription =
      operationItems.value("type").toObject().value("description").toString();
    CHECK(typeDescription.contains("Primitive modeling types"));
    CHECK(typeDescription.contains("skill recipes/IR"));
    CHECK_FALSE(typeDescription.contains("room,"));
    CHECK_FALSE(typeDescription.contains("corridor,"));
    CHECK_FALSE(typeDescription.contains("stairs,"));
    CHECK_FALSE(typeDescription.contains("doorway,"));
    CHECK_FALSE(typeDescription.contains("cover,"));
    CHECK_FALSE(typeDescription.contains("sky_shell"));
    CHECK(operationItems.value("metadata").isObject());
    CHECK(operationItems.value("parts").isObject());
    CHECK(operationItems.value("partMaterials").isObject());
    CHECK(operationItems.value("partMetadata").isObject());
    CHECK(operationItems.value("radius").isObject());
    CHECK(operationItems.value("rise").isObject());
    CHECK(operationItems.value("turnDegrees")
            .toObject()
            .value("description")
            .toString()
            .contains("360"));
    CHECK_FALSE(operationItems.value("doorMin").isObject());
    CHECK_FALSE(operationItems.value("doorMax").isObject());
    CHECK(operationItems.value("orderStart").isObject());
    CHECK(operationItems.value("orderStep").isObject());

    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& listedTool : tools)
    {
      names.push_back(listedTool.toObject().value("name").toString());
    }
    CHECK(names.contains("selector_preview"));
    CHECK(names.contains("objects_select_by_selector"));
    CHECK(names.contains("objects_delete_by_selector"));
    CHECK(names.contains("render_review_selector"));
    CHECK(names.contains("module_list"));
    CHECK(names.contains("module_inspect"));
    CHECK(!names.contains("module_select"));
    CHECK(names.contains("module_render_review"));
    CHECK(names.contains("module_validate"));
    CHECK(names.contains("module_compact"));
    CHECK(!names.contains("ir_validate"));
    CHECK(names.contains("ir_compile_preview"));
    CHECK(names.contains("ir_compile_preview_from_file"));
    CHECK(names.contains("ir_apply"));
    CHECK(names.contains("ir_apply_from_file"));

    const auto moduleList = findToolDefinition("module_list");
    REQUIRE(moduleList);
    CHECK(moduleList->description.contains("live modules only"));
    const auto moduleListProperties =
      moduleList->inputSchema.value("properties").toObject();
    CHECK(moduleListProperties.value("includeStale").isObject());
    CHECK(moduleListProperties.value("includeEmpty").isObject());
  }

  SECTION("legacy viewport review helper documents retirement path")
  {
    const auto tool = findToolDefinition("viewport_capture_scene_review");
    REQUIRE(tool);

    CHECK(tool->description.contains("Retired lightweight-runtime viewport helper"));
    CHECK(tool->description.contains("Searchable for migration only"));
    CHECK(tool->description.contains("render_review_selector"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("objectIds").isObject());
    CHECK(properties.value("operationIds").isObject());
    CHECK(properties.value("framing").isObject());
    CHECK(properties.value("bounds").isObject());
    CHECK(properties.value("layout").isObject());
    CHECK(properties.value("views").isObject());
    CHECK(properties.value("camera").isObject());
    CHECK(properties.value("isolate").isObject());
    CHECK(properties.value("isolateMode").isObject());
    CHECK(properties.value("min2dHeight").isObject());
    CHECK(properties.value("highlight").isObject());
    CHECK(properties.value("clearSelectionBeforeCapture").isObject());
  }

  SECTION("render review targets stays hidden behind higher-level review tools")
  {
    const auto targetTool = findToolDefinition("render_review_targets");
    REQUIRE(targetTool);

    CHECK(targetTool->description.contains("Low-level geometry review renderer"));
    CHECK(targetTool->description.contains("Prefer render_review_selector"));

    const auto targetProperties = targetTool->inputSchema.value("properties").toObject();
    CHECK(targetProperties.value("operationIds").isObject());
    CHECK(targetProperties.value("objectIds").isObject());
    CHECK(targetProperties.value("views").isObject());
    CHECK(targetProperties.value("style").isObject());
    CHECK(targetProperties.value("edgeMode").isObject());
    CHECK(targetProperties.value("edgeMode")
            .toObject()
            .value("description")
            .toString()
            .contains("silhouette"));
    CHECK(targetProperties.value("combineViews").isObject());
    CHECK(targetProperties.value("contactSheetSize").isObject());
    CHECK(targetProperties.value("contactSheetMaxCaptures").isObject());
    CHECK(targetProperties.value("contactSheetMaxCaptures")
            .toObject()
            .value("description")
            .toString()
            .contains("Defaults to 2"));
    CHECK(targetProperties.value("imageSize").isObject());
    CHECK(targetProperties.value("maxDetailedFaces").isObject());

    const auto currentSceneTool = findToolDefinition("render_review_current_scene");
    REQUIRE(currentSceneTool);
    CHECK(currentSceneTool->description.contains("active document"));
    CHECK(currentSceneTool->description.contains("preferredCapturePath"));
    CHECK(currentSceneTool->description.contains("renderReadable only checks"));
    CHECK(currentSceneTool->description.contains("qualityValid is a legacy alias"));

    const auto currentSceneProperties =
      currentSceneTool->inputSchema.value("properties").toObject();
    CHECK(currentSceneProperties.value("scope").isObject());
    CHECK(currentSceneProperties.value("preset").isObject());
    CHECK(currentSceneProperties.value("style").isObject());
    CHECK(currentSceneProperties.value("edgeMode").isObject());
    CHECK(currentSceneProperties.value("combineViews").isObject());
    CHECK(currentSceneProperties.value("contactSheetMaxCaptures").isObject());
    CHECK(currentSceneProperties.value("contactSheetMaxCaptures")
            .toObject()
            .value("description")
            .toString()
            .contains("Defaults to 2"));
    CHECK(currentSceneProperties.value("idsMode").isObject());
    CHECK(currentSceneProperties.value("detail").isObject());
    CHECK(currentSceneProperties.value("labelParts").isObject());

    const auto tool = findToolDefinition("render_review_operation");
    REQUIRE(tool);

    CHECK(tool->description.contains("isolated Agent-readable geometry review bundle"));
    CHECK(tool->description.contains("renderReadable only checks"));
    CHECK(tool->description.contains("qualityValid is a legacy alias"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("operationIds").isObject());
    CHECK(properties.value("objectIds").isObject());
    CHECK(properties.value("views").isObject());
    CHECK(properties.value("style").isObject());
    CHECK(properties.value("edgeMode").isObject());
    CHECK(properties.value("combineViews").isObject());
    CHECK(properties.value("contactSheetSize").isObject());
    CHECK(properties.value("contactSheetMaxCaptures").isObject());
    CHECK(properties.value("contactSheetMaxCaptures")
            .toObject()
            .value("description")
            .toString()
            .contains("Defaults to 2"));
    CHECK(properties.value("isolateMode").isObject());
    CHECK(properties.value("framingPreset").isObject());
    CHECK(properties.value("imageSize").isObject());
    CHECK(properties.value("idsMode").isObject());
    CHECK(properties.value("labelParts").isObject());

    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& listedTool : tools)
    {
      names.push_back(listedTool.toObject().value("name").toString());
    }
    CHECK(!names.contains("render_review_targets"));
    CHECK(names.contains("render_review_current_scene"));
    CHECK(names.contains("render_review_operation"));

    const auto searchResults = toolsSearchJson(
      "render_review_targets", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(searchResults.size() == 1);
    const auto found = searchResults.first().toObject();
    CHECK(found.value("name").toString() == "render_review_targets");
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());

    const auto selectorTool = findToolDefinition("render_review_selector");
    REQUIRE(selectorTool);
    CHECK(selectorTool->description.contains("renderReadable only checks"));
    CHECK(selectorTool->description.contains("qualityValid is a legacy alias"));
    CHECK(selectorTool->inputSchema.value("properties")
            .toObject()
            .value("labelParts")
            .isObject());

    const auto moduleReviewTool = findToolDefinition("module_render_review");
    REQUIRE(moduleReviewTool);
    CHECK(moduleReviewTool->description.contains("renderReadable only checks"));
    CHECK(moduleReviewTool->description.contains("qualityValid is a legacy alias"));
    CHECK(moduleReviewTool->inputSchema.value("properties")
            .toObject()
            .value("labelParts")
            .isObject());
  }

  SECTION("viewport camera set schema remains searchable for migration")
  {
    const auto tool = findToolDefinition("viewport_camera_set");
    REQUIRE(tool);

    CHECK(tool->description.contains("Retired lightweight-runtime viewport helper"));
    CHECK(tool->description.contains("render_review_selector"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("position").isObject());
    CHECK(properties.value("target").isObject());
    CHECK(properties.value("up").isObject());
    CHECK(properties.value("zoom").isObject());
    const auto required = tool->inputSchema.value("required").toArray();
    CHECK(required.contains("position"));
    CHECK(required.contains("target"));
  }

  SECTION("viewport camera frame bounds schema remains searchable for migration")
  {
    const auto tool = findToolDefinition("viewport_camera_frame_bounds");
    REQUIRE(tool);

    CHECK(tool->description.contains("Retired lightweight-runtime viewport helper"));
    CHECK(tool->description.contains("module_render_review"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("objectIds").isObject());
    CHECK(properties.value("bounds").isObject());
    CHECK(properties.value("min").isObject());
    CHECK(properties.value("max").isObject());
    CHECK(properties.value("azimuth").isObject());
    CHECK(properties.value("elevation").isObject());
    CHECK(properties.value("distanceScale").isObject());
    CHECK(properties.value("targetOffset").isObject());
  }

  SECTION("tool search can discover hidden expert tools")
  {
    const auto tools = toolsSearchJson(
      "from_planes", "brush", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "brush_create_from_planes")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "brush");
    CHECK(found.value("expert").toBool());
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("tool search omits schemas from broad schema queries")
  {
    const auto tools =
      toolsSearchJson("selector", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(tools.size() > 1);

    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      const auto name = object.value("name").toString();
      CAPTURE(name);
      CHECK_FALSE(object.value("inputSchema").isObject());
      CHECK(object.value("schemaAvailable").toBool());
      CHECK(object.value("schemaOmittedReason").toString() == "multiple_matches");
      CHECK(object.value("schemaQuery").toString() == name);
    }
  }

  SECTION("tool search matches tokenized schema queries")
  {
    const auto tools = toolsSearchJson(
      "blockout_create_batch operations box format",
      "blockout",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "blockout_create_batch")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "blockout");
    CHECK(found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());

    const auto schema = found.value("inputSchema").toObject();
    const auto operations =
      schema.value("properties").toObject().value("operations").toObject();
    CHECK(operations.value("description")
            .toString()
            .contains("offAxisRampMayProduceNonGridVertices"));
    CHECK(operations.value("items").isObject());
    const auto itemDescription =
      operations.value("items").toObject().value("description").toString();
    CHECK(itemDescription.contains(R"("type":"box")"));
    CHECK(itemDescription.contains(R"("type":"cylinder")"));
    CHECK(itemDescription.contains(R"("type":"curved_corridor")"));
    CHECK(itemDescription.contains(R"("type":"path_ribbon")"));
    CHECK(itemDescription.contains(R"("type":"ramp_between")"));
    CHECK(itemDescription.contains(R"("type":"wedge")"));
    CHECK(itemDescription.contains(R"("type":"repeat_translate")"));
    CHECK(itemDescription.contains(R"("type":"repeat_grid")"));
    CHECK(itemDescription.contains(R"("counts":6)"));
    CHECK(itemDescription.contains(R"("type":"stepped_mass")"));
    CHECK(itemDescription.contains(R"("type":"support_posts_between")"));
    CHECK(itemDescription.contains("skill recipes"));
    CHECK_FALSE(itemDescription.contains(R"("type":"room")"));
    CHECK_FALSE(itemDescription.contains(R"("type":"corridor")"));
    CHECK_FALSE(itemDescription.contains(R"("type":"stairs")"));
    CHECK_FALSE(itemDescription.contains(R"("type":"doorway")"));
    CHECK_FALSE(itemDescription.contains(R"("type":"cover")"));
    CHECK_FALSE(itemDescription.contains(R"("type":"sky_shell")"));
    CHECK(
      operations.value("items").toObject().value("required").toArray().contains("type"));

    const auto itemProperties =
      operations.value("items").toObject().value("properties").toObject();
    CHECK(itemProperties.value("sides").isObject());
    CHECK(itemProperties.value("axis")
            .toObject()
            .value("description")
            .toString()
            .contains("cylinder"));
    CHECK(itemProperties.value("count").isObject());
    CHECK(itemProperties.value("counts").isObject());
    CHECK(itemProperties.value("offset").isObject());
    CHECK(itemProperties.value("offsets").isObject());
    CHECK(itemProperties.value("levels").isObject());
    CHECK(itemProperties.value("inset").isObject());
    CHECK(itemProperties.value("stepHeight").isObject());
    CHECK(itemProperties.value("bottomZ").isObject());
    CHECK(itemProperties.value("topZ").isObject());
    CHECK(itemProperties.value("postSize").isObject());
    CHECK_FALSE(itemProperties.value("doorMin").isObject());
    CHECK_FALSE(itemProperties.value("doorMax").isObject());
    const auto childOperation = itemProperties.value("operation").toObject();
    CHECK(childOperation.value("type").toString() == "object");
    CHECK(childOperation.value("additionalProperties").toBool());
  }

  SECTION("tool search returns exact tool names from multi-tool queries")
  {
    const auto tools = toolsSearchJson(
      "brush_create_polygon_batch kz_distance_analyze_chain selection_by_metadata",
      "",
      "summary",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("selection_by_metadata"));
  }

  SECTION("tool search exact names survive category hints")
  {
    const auto tools = toolsSearchJson(
      "blockout_create_batch operations box format",
      "blockout",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("blockout_create_batch"));
  }

  SECTION("tool search exact names survive misleading category hints")
  {
    const auto tools = toolsSearchJson(
      "entity_create_checked_batch",
      "blockout",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("entity_create_checked_batch"));
  }

  SECTION("tool search exact tool name returns only exact matches")
  {
    const auto tools = toolsSearchJson(
      "blockout_create_batch", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(tools.size() == 1);
    CHECK(tools.first().toObject().value("name").toString() == "blockout_create_batch");
  }

  SECTION("tool search exact names can find hidden profile tools")
  {
    const auto tools = toolsSearchJson(
      "viewport_capture_3d", "", "summary", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(tools.size() == 1);
    const auto tool = tools.first().toObject();
    CHECK(tool.value("name").toString() == "viewport_capture_3d");
    CHECK(!tool.value("visibleInCurrentProfile").toBool());
  }

  SECTION("catalog keeps scene prefab growth out of implemented tools")
  {
    const auto bannedSceneToolNames = QStringList{
      "create_temple",
      "create_cottage",
      "create_kz_route",
      "create_courtyard",
      "create_racetrack",
      "temple_create",
      "cottage_create",
      "kz_route_create",
      "courtyard_create",
      "racetrack_create",
    };

    for (const auto& bannedName : bannedSceneToolNames)
    {
      CAPTURE(bannedName);
      CHECK(!findToolDefinition(bannedName));
    }

    for (const auto& removedName : removedPrefabConvenienceToolNames())
    {
      CAPTURE(removedName);
      CHECK(!findToolDefinition(removedName));
    }

    for (const auto& tool : defaultToolCatalog())
    {
      CAPTURE(tool.name);
      CHECK(!tool.name.contains("prefab", Qt::CaseInsensitive));
      CHECK(!tool.name.contains("temple", Qt::CaseInsensitive));
      CHECK(!tool.name.contains("cottage", Qt::CaseInsensitive));
      CHECK(!tool.name.contains("courtyard", Qt::CaseInsensitive));
      CHECK(!tool.name.contains("racetrack", Qt::CaseInsensitive));
    }

    const auto modelingTools =
      toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto modelingNames = QStringList{};
    for (const auto& tool : modelingTools)
    {
      modelingNames.push_back(tool.toObject().value("name").toString());
    }

    for (const auto& removedName : removedPrefabConvenienceToolNames())
    {
      CAPTURE(removedName);
      CHECK(!modelingNames.contains(removedName));
    }
    for (const auto& bannedName : bannedSceneToolNames)
    {
      CAPTURE(bannedName);
      CHECK(!modelingNames.contains(bannedName));
    }

    const auto allEditTools = toolsListJson(McpMode::Edit, false, McpToolProfile::Full);
    auto fullNames = QStringList{};
    for (const auto& tool : allEditTools)
    {
      fullNames.push_back(tool.toObject().value("name").toString());
    }
    for (const auto& removedName : removedPrefabConvenienceToolNames())
    {
      CAPTURE(removedName);
      CHECK(!fullNames.contains(removedName));

      const auto searchResults =
        toolsSearchJson(removedName, "", "schema", McpMode::Edit, McpToolProfile::Full);
      for (const auto& searchResult : searchResults)
      {
        CAPTURE(searchResult.toObject().value("name").toString());
        CHECK(searchResult.toObject().value("name").toString() != removedName);
      }
    }
  }

  SECTION(
    "catalog exposes lifecycle/category metadata and blocks arbitrary execution tools")
  {
    const auto knownCategories = QStringList{
      "asset",
      "blockout",
      "brush",
      "core",
      "entity",
      "general",
      "geometry",
      "group",
      "heightmap",
      "object",
      "operation",
      "python",
      "route",
      "selection",
      "texture",
      "viewport",
    };
    const auto knownLifecycles = QStringList{
      "deprecated",
      "experimental",
      "legacy",
      "stable",
    };
    const auto bannedExecutionToolNames = QStringList{
      "execute_trenchbroom_code",
      "execute_tb_code",
      "run_tb_script",
      "run_cpp",
      "eval_cpp",
      "eval_trenchbroom",
    };

    for (const auto& bannedName : bannedExecutionToolNames)
    {
      CAPTURE(bannedName);
      CHECK(!findToolDefinition(bannedName));
    }

    for (const auto& tool : defaultToolCatalog())
    {
      CAPTURE(tool.name);
      CHECK(knownCategories.contains(tool.category));
      CHECK(knownLifecycles.contains(tool.lifecycle));
      CHECK(!tool.lifecycle.isEmpty());
      CHECK(!tool.category.isEmpty());
      CHECK(!tool.name.contains("execute_trenchbroom", Qt::CaseInsensitive));
      CHECK(!tool.name.contains("run_cpp", Qt::CaseInsensitive));
    }

    const auto pythonTool = findToolDefinition("python_generate_blockout");
    REQUIRE(pythonTool);
    CHECK(pythonTool->lifecycle == "legacy");

    const auto unsupportedTool = findToolDefinition("brush_create_arch");
    REQUIRE(unsupportedTool);
    CHECK(unsupportedTool->lifecycle == "experimental");

    const auto summary = toolsSummaryJson(McpMode::Edit, true, McpToolProfile::Modeling);
    REQUIRE(!summary.isEmpty());
    CHECK(summary.first().toObject().contains("lifecycle"));
    CHECK(summary.first().toObject().contains("category"));

    const auto stats =
      toolProfileStatsJson(McpMode::Edit, true, McpToolProfile::Modeling);
    const auto categoryCounts = stats.value("toolCategoryCounts").toObject();
    const auto lifecycleCounts = stats.value("toolLifecycleCounts").toObject();
    CHECK(categoryCounts.value("brush").toInt() > 0);
    CHECK(lifecycleCounts.value("stable").toInt() > 0);
  }

  SECTION("retired convenience paths carry replacement guidance")
  {
    const auto expectedDescriptionFragments = std::map<QString, QStringList>{
      {"history_list", {"Hidden diagnostic", "history_status"}},
      {"operation_select", {"Hidden manual recovery", "selector_preview"}},
      {"objects_delete_by_filter", {"Expert destructive", "objects_delete_by_selector"}},
      {"objects_delete_by_operation", {"Compatibility helper", "selector/module"}},
      {"python_generate_blockout", {"Legacy script bridge", "ir_apply_from_file"}},
      {"brush_metadata_set", {"Legacy", "defaultMetadata"}},
      {"brush_metadata_get", {"Legacy", "selector_preview"}},
      {"selection_by_metadata", {"Legacy", "structured selectors"}},
      {"route_geometry_analyze_chain", {"Prefer", "geometry_analyze_route_continuity"}},
      {"kz_distance_analyze_chain",
       {"Compatibility alias", "geometry_analyze_route_continuity"}},
    };

    for (const auto& [toolName, fragments] : expectedDescriptionFragments)
    {
      CAPTURE(toolName);
      const auto tool = findToolDefinition(toolName);
      REQUIRE(tool);
      for (const auto& fragment : fragments)
      {
        CAPTURE(fragment);
        CHECK(tool->description.contains(fragment));
      }
    }
  }

  SECTION("safe batch modeling helpers have structured schemas")
  {
    const auto boxesTool = findToolDefinition("brush_create_boxes_batch");
    REQUIRE(boxesTool);
    CHECK(boxesTool->requiredMode == McpMode::Edit);
    CHECK(boxesTool->mutatesDocument);
    CHECK(boxesTool->category == "brush");
    const auto boxesSchema = boxesTool->inputSchema.value("properties").toObject();
    CHECK(boxesSchema.value("boxes").toObject().value("items").isObject());
    CHECK(boxesSchema.value("boxes")
            .toObject()
            .value("items")
            .toObject()
            .value("required")
            .toArray()
            .contains("min"));

    const auto polygonTool = findToolDefinition("brush_create_polygon_batch");
    REQUIRE(polygonTool);
    CHECK(polygonTool->requiredMode == McpMode::Edit);
    CHECK(polygonTool->mutatesDocument);
    CHECK(polygonTool->category == "brush");
    CHECK(polygonTool->description.contains("polygonDiagnostics"));
    const auto polygonSchema = polygonTool->inputSchema.value("properties").toObject();
    const auto polygonItem =
      polygonSchema.value("brushes").toObject().value("items").toObject();
    CHECK(polygonItem.value("required").toArray().contains("points2d"));
    CHECK(polygonItem.value("required").toArray().contains("minZ"));
    CHECK(polygonItem.value("required").toArray().contains("maxZ"));
    CHECK(polygonItem.value("properties").toObject().contains("metadata"));
    const auto metadataSchema =
      polygonItem.value("properties").toObject().value("metadata").toObject();
    CHECK(metadataSchema.value("additionalProperties").toBool());
    CHECK(metadataSchema.value("description").toString().contains("Custom"));
    CHECK(metadataSchema.value("properties").toObject().contains("order"));

    const auto deleteTool = findToolDefinition("objects_delete_by_filter");
    REQUIRE(deleteTool);
    CHECK(deleteTool->category == "object");
    CHECK(deleteTool->requiredMode == McpMode::Edit);
    CHECK(
      deleteTool->inputSchema.value("properties").toObject().value("idsMode").isObject());

    const auto deleteOperationTool = findToolDefinition("objects_delete_by_operation");
    REQUIRE(deleteOperationTool);
    CHECK(deleteOperationTool->inputSchema.value("required")
            .toArray()
            .contains("operationId"));
    CHECK(deleteOperationTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto boxesBatchTool = findToolDefinition("brush_create_boxes_batch");
    REQUIRE(boxesBatchTool);
    CHECK(boxesBatchTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto blockoutBatchTool = findToolDefinition("blockout_create_batch");
    REQUIRE(blockoutBatchTool);
    CHECK(blockoutBatchTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto operationInspectTool = findToolDefinition("operation_inspect");
    REQUIRE(operationInspectTool);
    CHECK(operationInspectTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto operationValidateTool = findToolDefinition("operation_validate");
    REQUIRE(operationValidateTool);
    CHECK(operationValidateTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto undoToTool = findToolDefinition("history_undo_to_operation");
    REQUIRE(undoToTool);
    CHECK(undoToTool->requiredMode == McpMode::Edit);
    CHECK(undoToTool->mutatesDocument);
    CHECK(undoToTool->inputSchema.value("required").toArray().contains("operationId"));

    const auto transformTool = findToolDefinition("objects_transform");
    REQUIRE(transformTool);
    const auto transformProperties =
      transformTool->inputSchema.value("properties").toObject();
    CHECK(transformProperties.value("operationId").isObject());
    CHECK(transformProperties.value("operationIds").isObject());
    CHECK(transformProperties.value("selector").isObject());
    CHECK(transformProperties.value("idsMode").isObject());
    CHECK(transformProperties.value("sampleLimit").isObject());
    CHECK(transformTool->description.contains("current user selection"));
    CHECK(transformTool->description.contains("selector"));
    CHECK(transformTool->description.contains("stretched"));
    CHECK(transformTool->inputSchema.value("required").toArray().contains("operation"));
    CHECK(!transformTool->inputSchema.value("required").toArray().contains("objectIds"));

    const auto textureTool = findToolDefinition("texture_apply_by_filter");
    REQUIRE(textureTool);
    CHECK(textureTool->category == "texture");
    CHECK(textureTool->inputSchema.value("required").toArray().contains("material"));
    const auto textureProperties =
      textureTool->inputSchema.value("properties").toObject();
    CHECK(textureProperties.value("operationId").isObject());
    CHECK(textureProperties.value("operationIds").isObject());
    CHECK(textureProperties.value("faceSemantic").isObject());
    CHECK(textureProperties.value("normal").isObject());
    CHECK(textureProperties.value("idsMode").isObject());

    const auto routeContinuityTool =
      findToolDefinition("geometry_analyze_route_continuity");
    REQUIRE(routeContinuityTool);
    CHECK(routeContinuityTool->description.contains("verticalStep"));
    CHECK(routeContinuityTool->description.contains("horizontalGap"));
    CHECK(routeContinuityTool->description.contains("overlap_continuous_height"));
    CHECK(routeContinuityTool->description.contains("current user-selected brushes"));
    const auto routeContinuityProperties =
      routeContinuityTool->inputSchema.value("properties").toObject();
    CHECK(routeContinuityProperties.value("operationId").isObject());
    CHECK(routeContinuityProperties.value("operationIds").isObject());
    CHECK(routeContinuityProperties.value("objectIds").isObject());
    CHECK(routeContinuityProperties.value("selector").isObject());
    CHECK(routeContinuityProperties.value("selector")
            .toObject()
            .value("description")
            .toString()
            .contains("role:\"walkable\""));
    CHECK(routeContinuityProperties.value("routeDirection").isObject());
    CHECK(routeContinuityProperties.value("start").isObject());
    CHECK(routeContinuityProperties.value("end").isObject());
    CHECK(routeContinuityProperties.value("verticalTolerance").isObject());
    CHECK(routeContinuityProperties.value("horizontalTolerance").isObject());
    CHECK(routeContinuityProperties.value("continuityMode").isObject());
    CHECK(routeContinuityProperties.value("routeMode").isObject());
    CHECK(routeContinuityProperties.value("validationMode").isObject());
    CHECK(routeContinuityProperties.value("maxStepHeight").isObject());
    CHECK(routeContinuityProperties.value("maxJumpGap").isObject());
    CHECK(routeContinuityProperties.value("qualityPolicy").isObject());
    CHECK(routeContinuityProperties.value("detail").isObject());

    const auto slopeTool = findToolDefinition("geometry_analyze_slopes");
    REQUIRE(slopeTool);
    CHECK(slopeTool->description.contains("current user-selected brushes"));
    const auto slopeProperties = slopeTool->inputSchema.value("properties").toObject();
    CHECK(slopeProperties.value("selector").isObject());
    CHECK(slopeProperties.value("detail").isObject());

    const auto faceTextureTool = findToolDefinition("face_texture_set");
    REQUIRE(faceTextureTool);
    const auto faceTextureProperties =
      faceTextureTool->inputSchema.value("properties").toObject();
    CHECK(faceTextureProperties.value("operationIds").isObject());
    CHECK(faceTextureProperties.value("faceSemantic").isObject());
    CHECK(faceTextureProperties.value("idsMode").isObject());

    const auto assetPlaceTool = findToolDefinition("asset_place_model");
    REQUIRE(assetPlaceTool);
    CHECK(assetPlaceTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto reviewTargetsTool = findToolDefinition("render_review_targets");
    REQUIRE(reviewTargetsTool);
    const auto reviewTargetProperties =
      reviewTargetsTool->inputSchema.value("properties").toObject();
    CHECK(reviewTargetProperties.value("preset").isObject());
    CHECK(reviewTargetProperties.value("verticalExaggeration").isObject());
    CHECK(reviewTargetProperties.value("includeEntityLabels").isObject());
    CHECK(reviewTargetProperties.value("includeEntityLabels")
            .toObject()
            .value("description")
            .toString()
            .contains("auto-hide"));
    CHECK(reviewTargetProperties.value("includeOrderLabels").isObject());
    CHECK(reviewTargetProperties.value("includeDirectionLabels").isObject());
    CHECK(reviewTargetProperties.value("labelStride").isObject());
    CHECK(reviewTargetProperties.value("labelParts").isObject());
    CHECK(reviewTargetProperties.value("autoHideLabelsThreshold").isObject());
    CHECK(reviewTargetProperties.value("autoHideLabelsThreshold")
            .toObject()
            .value("description")
            .toString()
            .contains("entity"));
    CHECK(reviewTargetProperties.value("contactSheetMaxCaptures").isObject());

    const auto validateTool = findToolDefinition("map_validate");
    REQUIRE(validateTool);
    const auto validateProperties =
      validateTool->inputSchema.value("properties").toObject();
    CHECK(validateProperties.value("includeProblems").isObject());
    CHECK(validateProperties.value("groupByType").isObject());
    CHECK(validateProperties.value("limit").isObject());

    const auto routeContinuityToolForOrder =
      findToolDefinition("geometry_analyze_route_continuity");
    REQUIRE(routeContinuityToolForOrder);
    const auto routeContinuityOrderProperties =
      routeContinuityToolForOrder->inputSchema.value("properties").toObject();
    CHECK(routeContinuityOrderProperties.value("orderBy").isObject());
    CHECK(routeContinuityOrderProperties.value("closedLoop").isObject());
    CHECK(routeContinuityOrderProperties.value("routeMode").isObject());

    const auto updatePropsTool = findToolDefinition("entity_properties_update");
    REQUIRE(updatePropsTool);
    CHECK(
      updatePropsTool->inputSchema.value("required").toArray().contains("properties"));
    CHECK(updatePropsTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto deletePropsTool = findToolDefinition("entity_properties_delete");
    REQUIRE(deletePropsTool);
    CHECK(deletePropsTool->inputSchema.value("required").toArray().contains("keys"));
    CHECK(deletePropsTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());

    const auto entityTool = findToolDefinition("entity_create_checked");
    REQUIRE(entityTool);
    CHECK(entityTool->category == "entity");
    CHECK(entityTool->inputSchema.value("required").toArray().contains("classname"));

    const auto entityBatchTool = findToolDefinition("entity_create_checked_batch");
    REQUIRE(entityBatchTool);
    CHECK(entityBatchTool->category == "entity");
    CHECK(entityBatchTool->inputSchema.value("required").toArray().contains("entities"));
    const auto entityBatchProperties =
      entityBatchTool->inputSchema.value("properties").toObject();
    CHECK(entityBatchProperties.value("entities").toObject().value("items").isObject());
    CHECK(entityBatchProperties.value("idsMode").isObject());

    const auto problemsFixTool = findToolDefinition("problems_fix");
    REQUIRE(problemsFixTool);
    CHECK(problemsFixTool->inputSchema.value("properties")
            .toObject()
            .value("idsMode")
            .isObject());
  }

  SECTION("safe batch modeling helpers share idsMode wording")
  {
    const auto idsModeDescription = [](const QString& toolName) {
      const auto tool = findToolDefinition(toolName);
      REQUIRE(tool);
      return tool->inputSchema.value("properties")
        .toObject()
        .value("idsMode")
        .toObject()
        .value("description")
        .toString();
    };

    const auto description = idsModeDescription("brush_create_boxes_batch");
    CHECK(description.contains("Defaults to count"));
    CHECK(description.contains("full maps to detail=ids"));
    CHECK(idsModeDescription("brush_create_polygon_batch") == description);
    CHECK(idsModeDescription("blockout_create_batch") == description);
  }

  SECTION("compact output tools share detail wording")
  {
    const auto detailDescription = [](const QString& toolName) {
      const auto tool = findToolDefinition(toolName);
      REQUIRE(tool);
      return tool->inputSchema.value("properties")
        .toObject()
        .value("detail")
        .toObject()
        .value("description")
        .toString();
    };

    const auto idsDetail = detailDescription("blockout_create_batch");
    CHECK(idsDetail == "summary, ids, or full. Defaults to summary.");
    CHECK(detailDescription("brush_create_boxes_batch") == idsDetail);
    CHECK(detailDescription("brush_create_polygon_batch") == idsDetail);
    CHECK(detailDescription("operation_inspect") == idsDetail);

    const auto fullDetail = detailDescription("module_render_review");
    CHECK(fullDetail == "summary or full. Defaults to summary.");
    CHECK(detailDescription("operation_validate") == fullDetail);
  }

  SECTION("selector and module tools share moduleId wording")
  {
    const auto propertyDescription =
      [](const QString& toolName, const QString& propertyName) {
        const auto tool = findToolDefinition(toolName);
        REQUIRE(tool);
        return tool->inputSchema.value("properties")
          .toObject()
          .value(propertyName)
          .toObject()
          .value("description")
          .toString();
      };

    const auto selectorTool = findToolDefinition("selector_preview");
    REQUIRE(selectorTool);
    const auto selectorModuleIdDescription = selectorTool->inputSchema.value("properties")
                                               .toObject()
                                               .value("selector")
                                               .toObject()
                                               .value("properties")
                                               .toObject()
                                               .value("moduleId")
                                               .toObject()
                                               .value("description")
                                               .toString();

    CHECK(selectorModuleIdDescription == "Session metadata moduleId.");
    CHECK(
      propertyDescription("module_inspect", "moduleId") == selectorModuleIdDescription);
    CHECK(
      propertyDescription("module_select", "moduleId") == selectorModuleIdDescription);
    CHECK(
      propertyDescription("module_render_review", "moduleId")
      == selectorModuleIdDescription);
    CHECK(
      propertyDescription("module_validate", "moduleId") == selectorModuleIdDescription);
    CHECK(
      propertyDescription("module_compact", "moduleId") == selectorModuleIdDescription);
  }

  SECTION("selector schema keeps metadata fields together")
  {
    const auto selectorTool = findToolDefinition("selector_preview");
    REQUIRE(selectorTool);
    const auto selectorProperties = selectorTool->inputSchema.value("properties")
                                      .toObject()
                                      .value("selector")
                                      .toObject()
                                      .value("properties")
                                      .toObject();

    const auto metadataFields = QStringList{
      "moduleId",
      "part",
      "role",
      "order",
      "routeId",
      "temporary",
      "generatedBy",
    };
    for (const auto& field : metadataFields)
    {
      CAPTURE(field);
      CHECK(selectorProperties.value(field).isObject());
      CHECK(selectorProperties.value(field)
              .toObject()
              .value("description")
              .toString()
              .contains("Session metadata"));
    }
  }

  SECTION("recovery tools share live operation target wording")
  {
    const auto propertyDescription =
      [](const QString& toolName, const QString& propertyName) {
        const auto tool = findToolDefinition(toolName);
        REQUIRE(tool);
        return tool->inputSchema.value("properties")
          .toObject()
          .value(propertyName)
          .toObject()
          .value("description")
          .toString();
      };

    const auto transformOperationId =
      propertyDescription("objects_transform", "operationId");
    const auto transformOperationIds =
      propertyDescription("objects_transform", "operationIds");
    CHECK(transformOperationId.contains("live changed objects"));
    CHECK(transformOperationIds.contains("live changed objects"));

    const auto slopeOperationId =
      propertyDescription("geometry_analyze_slopes", "operationId");
    const auto slopeOperationIds =
      propertyDescription("geometry_analyze_slopes", "operationIds");
    CHECK(slopeOperationId.contains("live changed brush objects"));
    CHECK(slopeOperationIds.contains("live changed brush objects"));
    CHECK(
      propertyDescription("geometry_analyze_route_continuity", "operationId")
      == slopeOperationId);
    CHECK(
      propertyDescription("geometry_analyze_route_continuity", "operationIds")
      == slopeOperationIds);

    const auto faceTextureTool = findToolDefinition("face_texture_set");
    REQUIRE(faceTextureTool);
    const auto faceTextureProperties =
      faceTextureTool->inputSchema.value("properties").toObject();
    CHECK(
      faceTextureProperties.value("operationId")
        .toObject()
        .value("description")
        .toString()
      == slopeOperationId);
    CHECK(
      faceTextureProperties.value("operationIds")
        .toObject()
        .value("description")
        .toString()
      == slopeOperationIds);
  }

  SECTION("mode gating rejects edit tools in read-only mode")
  {
    const auto editTool = findToolDefinition("entity_create");
    const auto polygonBatchTool = findToolDefinition("brush_create_polygon_batch");
    const auto metadataSetTool = findToolDefinition("brush_metadata_set");
    const auto metadataGetTool = findToolDefinition("brush_metadata_get");
    const auto routeGeometryTool = findToolDefinition("route_geometry_analyze_chain");
    const auto kzDistanceTool = findToolDefinition("kz_distance_analyze_chain");

    REQUIRE(editTool);
    REQUIRE(polygonBatchTool);
    REQUIRE(metadataSetTool);
    REQUIRE(metadataGetTool);
    REQUIRE(routeGeometryTool);
    REQUIRE(kzDistanceTool);
    CHECK(!canCallTool(*editTool, McpMode::ReadOnly));
    CHECK(canCallTool(*editTool, McpMode::Edit));
    CHECK(!canCallTool(*polygonBatchTool, McpMode::ReadOnly));
    CHECK(canCallTool(*polygonBatchTool, McpMode::Edit));
    CHECK(!canCallTool(*metadataSetTool, McpMode::ReadOnly));
    CHECK(canCallTool(*metadataSetTool, McpMode::Edit));
    CHECK(canCallTool(*metadataGetTool, McpMode::ReadOnly));
    CHECK(canCallTool(*routeGeometryTool, McpMode::ReadOnly));
    CHECK(canCallTool(*kzDistanceTool, McpMode::ReadOnly));
  }

  SECTION("route metadata legacy tools are hidden and searchable")
  {
    const auto editTools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto editNames = QStringList{};
    for (const auto& tool : editTools)
    {
      editNames.push_back(tool.toObject().value("name").toString());
    }
    CHECK(!editNames.contains("shape_library_list"));
    CHECK(editNames.contains("brush_create_polygon_batch"));
    CHECK(!editNames.contains("brush_metadata_set"));
    CHECK(!editNames.contains("brush_metadata_get"));
    CHECK(!editNames.contains("selection_by_metadata"));
    CHECK(!editNames.contains("route_geometry_analyze_chain"));
    CHECK(!editNames.contains("kz_distance_analyze_chain"));

    const auto searchResults = toolsSearchJson(
      "route geometry", "route", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : searchResults)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "route_geometry_analyze_chain")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("description")
            .toString()
            .contains("geometry_analyze_route_continuity"));
    CHECK_FALSE(found.value("inputSchema").isObject());
    CHECK(found.value("schemaAvailable").toBool());
    CHECK(found.value("schemaOmittedReason").toString() == "multiple_matches");
    CHECK(found.value("schemaQuery").toString() == "route_geometry_analyze_chain");

    const auto exactRouteResults = toolsSearchJson(
      "route_geometry_analyze_chain",
      "",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    REQUIRE(exactRouteResults.size() == 1);
    CHECK(exactRouteResults.first()
            .toObject()
            .value("inputSchema")
            .toObject()
            .value("properties")
            .toObject()
            .contains("routeId"));

    const auto metadataResults = toolsSearchJson(
      "selection_by_metadata", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(metadataResults.size() == 1);
    const auto metadataTool = metadataResults.first().toObject();
    CHECK(metadataTool.value("name").toString() == "selection_by_metadata");
    CHECK(!metadataTool.value("visibleInCurrentProfile").toBool());
    CHECK(metadataTool.value("description").toString().contains("Prefer structured"));

    const auto aliasResults = toolsSearchJson(
      "kz_distance_analyze_chain", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto alias = QJsonObject{};
    for (const auto& tool : aliasResults)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "kz_distance_analyze_chain")
      {
        alias = object;
        break;
      }
    }

    REQUIRE(!alias.isEmpty());
    CHECK(!alias.value("visibleInCurrentProfile").toBool());
    CHECK(alias.value("description")
            .toString()
            .contains("geometry_analyze_route_continuity"));

    const auto metadataSetResults = toolsSearchJson(
      "brush_metadata_set", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(metadataSetResults.size() == 1);
    const auto metadataSetTool = metadataSetResults.first().toObject();
    CHECK(!metadataSetTool.value("visibleInCurrentProfile").toBool());
    CHECK(metadataSetTool.value("description").toString().contains("Legacy"));
    CHECK(metadataSetTool.value("description").toString().contains("defaultMetadata"));
  }

  SECTION("tool json uses MCP inputSchema shape")
  {
    const auto tool = findToolDefinition("map_search");

    REQUIRE(tool);
    const auto json = toMcpToolJson(*tool);

    CHECK(json.value("name").toString() == "map_search");
    CHECK(json.value("inputSchema").toObject().value("type").toString() == "object");
  }

  SECTION("overlay_set schema exposes marker inputs")
  {
    const auto tool = findToolDefinition("overlay_set");

    REQUIRE(tool);
    const auto properties = tool->inputSchema.value("properties").toObject();

    CHECK(properties.contains("highlightObjectIds"));
    CHECK(properties.contains("labels"));
    CHECK(properties.contains("pointMarkers"));
    CHECK(properties.contains("boundsMarkers"));
  }

  SECTION("tool diagnostics include unsupported roadmap tools")
  {
    const auto diagnostics = toolDiagnosticsJson(McpMode::Edit);
    auto archDiagnostic = QJsonObject{};
    for (const auto& entry : diagnostics)
    {
      const auto object = entry.toObject();
      if (object.value("name").toString() == "brush_create_arch")
      {
        archDiagnostic = object;
        break;
      }
    }

    REQUIRE(!archDiagnostic.isEmpty());
    CHECK(archDiagnostic.value("requiredMode").toString() == "Edit");
    CHECK(archDiagnostic.value("availableInCurrentMode").toBool());
    CHECK(archDiagnostic.value("mutatesDocument").toBool());
    CHECK_FALSE(archDiagnostic.value("implemented").toBool());
    CHECK(archDiagnostic.value("category").toString() == "brush");
    CHECK(archDiagnostic.value("expert").toBool());
    CHECK(archDiagnostic.value("lifecycle").toString() == "experimental");
  }
}

} // namespace tb::mcp
