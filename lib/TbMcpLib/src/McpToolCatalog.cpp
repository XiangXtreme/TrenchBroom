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

#include "mcp/McpToolCatalog.h"

#include <QJsonDocument>
#include <QStringList>

#include "McpToolCatalogInternal.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>

namespace tb::mcp
{
namespace
{

QJsonObject objectSchema(QJsonObject properties = {}, QJsonArray required = {})
{
  return QJsonObject{
    {"type", "object"},
    {"properties", properties},
    {"required", required},
    {"additionalProperties", false},
  };
}

QJsonObject stringProperty(const QString& description)
{
  return QJsonObject{
    {"type", "string"},
    {"description", description},
  };
}

QJsonObject numberProperty(const QString& description)
{
  return QJsonObject{
    {"type", "number"},
    {"description", description},
  };
}

QJsonObject integerProperty(const QString& description)
{
  return QJsonObject{
    {"type", "integer"},
    {"description", description},
  };
}

QJsonObject boolProperty(const QString& description)
{
  return QJsonObject{
    {"type", "boolean"},
    {"description", description},
  };
}

QJsonObject arrayProperty(const QString& description)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
  };
}

QJsonObject arrayProperty(const QString& description, const QJsonObject& items)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
    {"items", items},
  };
}

QJsonObject changedIdsModeProperty()
{
  return stringProperty(
    "none, count, sample, or full for changed object ids. Defaults to count; full "
    "maps to detail=ids.");
}

QJsonObject summaryFullDetailProperty()
{
  return stringProperty("summary or full. Defaults to summary.");
}

QJsonObject summaryIdsFullDetailProperty()
{
  return stringProperty("summary, ids, or full. Defaults to summary.");
}

QJsonObject moduleIdProperty()
{
  return stringProperty("Session metadata moduleId.");
}

QJsonObject liveOperationIdTargetProperty(const QString& objectKind = "objects")
{
  return stringProperty(
    QString("Optional MCP operation id whose live changed %1 are used as targets.")
      .arg(objectKind));
}

QJsonObject liveOperationIdsTargetProperty(const QString& objectKind = "objects")
{
  return arrayProperty(
    QString("Optional MCP operation ids whose live changed %1 are used as targets.")
      .arg(objectKind));
}

QJsonObject vec3Property(const QString& description)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
    {"items", QJsonObject{{"type", "number"}}},
    {"minItems", 3},
    {"maxItems", 3},
  };
}

QJsonObject numberOrVec3Property(const QString& description)
{
  return QJsonObject{
    {"description", description},
    {"oneOf",
     QJsonArray{
       QJsonObject{{"type", "number"}},
       QJsonObject{
         {"type", "array"},
         {"items", QJsonObject{{"type", "number"}}},
         {"minItems", 3},
         {"maxItems", 3},
       },
     }},
  };
}

QJsonObject stringOrVec3Property(const QString& description)
{
  return QJsonObject{
    {"description", description},
    {"oneOf",
     QJsonArray{
       QJsonObject{{"type", "string"}},
       QJsonObject{
         {"type", "array"},
         {"items", QJsonObject{{"type", "number"}}},
         {"minItems", 3},
         {"maxItems", 3},
       },
     }},
  };
}

QJsonObject stringObjectProperty(const QString& description)
{
  return QJsonObject{
    {"type", "object"},
    {"description", description},
    {"additionalProperties", QJsonObject{{"type", "string"}}},
  };
}

QJsonObject routeMetadataSchema()
{
  return QJsonObject{
    {"type", "object"},
    {"description",
     "Session-level route/object metadata. Known keys include routeId, intent, "
     "difficulty, movementType, order, takeoffEdge, landingWindow, incomingDirection, "
     "and outgoingDirection. Custom session-only keys are allowed for agent probes."},
    {"additionalProperties", true},
    {"properties",
     QJsonObject{
       {"routeId", stringProperty("Route or chain id, e.g. intro_route_a.")},
       {"intent",
        stringProperty("Mapper intent, e.g. redirect_right or precision_land.")},
       {"difficulty", stringProperty("Difficulty label such as easy, average, hard.")},
       {"movementType", stringProperty("Movement or route label such as bhop or ramp.")},
       {"order", numberProperty("Optional numeric order within a route chain.")},
       {"takeoffEdge", stringProperty("Semantic takeoff edge id or label.")},
       {"landingWindow", stringProperty("Semantic landing window id or label.")},
       {"incomingDirection", vec3Property("Incoming route direction vector.")},
       {"outgoingDirection", vec3Property("Outgoing route direction vector.")},
     }},
  };
}

QJsonObject faceTargetSchemaProperties()
{
  return QJsonObject{
    {"objectId",
     stringProperty("Optional single brush object id. If omitted, uses selection.")},
    {"objectIds",
     arrayProperty(
       "Optional brush, brush entity, group, or operation target object ids. All child "
       "brush faces are candidates.")},
    {"operationId", liveOperationIdTargetProperty("brush objects")},
    {"operationIds", liveOperationIdsTargetProperty("brush objects")},
    {"faceIndex",
     integerProperty("Optional face index when objectId names a single brush.")},
    {"idsMode",
     stringProperty(
       "none, count, sample, or full for changed object ids. Defaults to count; use "
       "full only when the caller must carry ids forward.")},
  };
}

QJsonObject semanticFaceSchemaProperties()
{
  return QJsonObject{
    {"faceSemantic",
     stringProperty(
       "Optional semantic face filter after resolving candidates: all, top, bottom, or "
       "side.")},
    {"normal",
     vec3Property(
       "Optional normal vector filter after resolving candidates. Faces whose normal "
       "dot this vector is at least normalTolerance are selected.")},
    {"normalTolerance",
     numberProperty(
       "Dot-product threshold for normal/semantic face matching, defaults to 0.75.")},
  };
}

QJsonObject mergeProperties(QJsonObject base, const QJsonObject& extra)
{
  for (auto it = extra.begin(); it != extra.end(); ++it)
  {
    base.insert(it.key(), it.value());
  }
  return base;
}

void addExpectedDocumentPathGuardSchema(McpToolDefinition& tool)
{
  if (!tool.mutatesDocument)
  {
    return;
  }

  auto properties = tool.inputSchema.value("properties").toObject();
  properties.insert(
    "expectedDocumentPath",
    stringProperty(
      "Optional active document path guard. If set, the tool refuses to mutate when "
      "the current active document path differs."));
  properties.insert(
    "expectedDocumentFingerprint",
    stringProperty(
      "Optional active document fingerprint guard. If set, the tool refuses to "
      "mutate when the current document identity differs. When path and fingerprint "
      "are both provided, both must match."));
  tool.inputSchema.insert("properties", properties);
}

QJsonObject polygonBatchItemSchema()
{
  return objectSchema(
    {
      {"points2d", arrayProperty("Convex 2D footprint points as [x,y].")},
      {"minZ", numberProperty("Minimum platform Z in map units.")},
      {"maxZ", numberProperty("Maximum platform Z in map units.")},
      {"material", stringProperty("Optional per-platform material override.")},
      {"metadata", routeMetadataSchema()},
    },
    {"points2d", "minZ", "maxZ"});
}

QJsonObject genericMetadataSchema()
{
  return QJsonObject{
    {"type", "object"},
    {"description",
     "Session-level Agent metadata. Common keys: moduleId, part, role, order, "
     "routeId, temporary, generatedBy. Stored by MCP for selector/module lookup; "
     "not written to the .map."},
    {"additionalProperties", true},
  };
}

QJsonObject qualityPolicySchema()
{
  return objectSchema({
    {"intent",
     stringProperty(
       "Curve approximation intent: draft, balanced, or smooth. Balanced is the "
       "default. Draft/balanced threshold misses warn; smooth threshold misses fail "
       "acceptance.")},
    {"maxDirectionChangeDegrees",
     numberProperty("Optional positive maximum direction change per segment.")},
    {"maxSagitta",
     numberProperty("Optional positive maximum arc-to-chord error in map units.")},
    {"maxSnapDisplacement",
     numberProperty("Optional positive maximum vertex snap displacement in map units.")},
  });
}

QJsonObject selectorMetadataProperties()
{
  return QJsonObject{
    {"moduleId", moduleIdProperty()},
    {"part", stringProperty("Session metadata part.")},
    {"role", stringProperty("Session metadata role.")},
    {"order", numberProperty("Session metadata order.")},
    {"routeId", stringProperty("Session metadata routeId.")},
    {"temporary", boolProperty("Session metadata temporary flag.")},
    {"generatedBy", stringProperty("Session metadata generatedBy value.")},
  };
}

QJsonObject selectorSchema()
{
  auto properties = selectorMetadataProperties();
  properties.insert("metadata", genericMetadataSchema());
  properties.insert("operationId", stringProperty("Single MCP operation id target."));
  properties.insert("operationIds", arrayProperty("MCP operation id targets."));
  properties.insert(
    "type", stringProperty("Node type filter, e.g. brush, entity, group."));
  properties.insert("classname", stringProperty("Entity classname filter."));
  properties.insert("targetname", stringProperty("Entity targetname filter."));
  properties.insert("material", stringProperty("Brush material filter."));
  properties.insert(
    "query", stringProperty("Text query over id/type/name/entity properties."));
  properties.insert("min", vec3Property("Optional bounds minimum."));
  properties.insert("max", vec3Property("Optional bounds maximum."));
  properties.insert("boundsMode", stringProperty("Bounds mode: intersects or contains."));
  properties.insert(
    "limit", integerProperty("Maximum matched objects, defaults to 100."));

  return QJsonObject{
    {"type", "object"},
    {"description",
     "Structured selector v1. Matches live objects by metadata, type, bounds, "
     "material, classname/targetname, operationId/operationIds, or moduleId. This is "
     "intentionally JSON, not a free-text DSL."},
    {"properties", properties},
    {"additionalProperties", false},
  };
}

const QStringList& knownToolCategories()
{
  static const auto Categories = QStringList{
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
  return Categories;
}

const QStringList& knownToolLifecycles()
{
  static const auto Lifecycles = QStringList{
    "deprecated",
    "experimental",
    "legacy",
    "stable",
  };
  return Lifecycles;
}

void assertKnownToolMetadata(const McpToolDefinition& tool)
{
  Q_ASSERT(knownToolCategories().contains(tool.category));
  Q_ASSERT(knownToolLifecycles().contains(tool.lifecycle));
}

QJsonObject withDescription(QJsonObject schema, const QString& description)
{
  schema.insert("description", description);
  return schema;
}

QJsonObject blockoutBatchOperationSchema()
{
  return QJsonObject{
    {"type", "object"},
    {"description",
     "Typed Blockout IR operation object. Primitive modeling operations should be "
     "preferred for ordinary Agent-generated geometry. Room, corridor, sky shell, "
     "doorway, cover, and straight stair composition moved to skill recipes that "
     "emit IR. Use explicit box/prism/polyhedron/brush_create_polygon_batch "
     "primitives for custom structures. Primitive examples: "
     R"({"type":"box","min":[0,0,0],"max":[128,128,16],"material":"clip"}; )"
     R"({"type":"cylinder","min":[-64,-64,0],"max":[64,64,128],)"
     R"("sides":16,"axis":"z"}; )"
     R"({"type":"prism","points2d":[[0,0],[128,0],[64,64]],"minZ":0,"maxZ":64}; )"
     R"({"type":"cylinder_sector","center":[0,0,0],"innerRadius":64,)"
     R"("outerRadius":128,"startAngle":0,"endAngle":90,"minZ":0,"maxZ":16}; )"
     R"({"type":"path_ribbon","points2d":[[0,0],[512,0],[768,256]],)"
     R"("width":160,"minZ":0,"maxZ":16}; )"
     R"({"type":"repeat_translate","count":4,"offset":[128,0,0],)"
     R"("operation":{"type":"box","min":[0,0,0],"max":[64,64,16]}}; )"
     R"({"type":"repeat_grid","counts":[4,3],"offsets":[[128,0,0],[0,0,96]],)"
     R"("operation":{"type":"box","min":[0,0,0],"max":[64,16,48]}}; )"
     R"({"type":"repeat_grid","counts":6,"offsets":[128,0,0],)"
     R"("operation":{"type":"box","min":[0,0,0],"max":[64,16,48]}}; )"
     R"({"type":"stepped_mass","min":[-512,-512,0],"max":[512,512,64],)"
     R"("levels":5,"inset":96,"stepHeight":64}; )"
     R"({"type":"support_posts_between","points2d":[[-256,-256],[256,-256]],)"
     R"("bottomZ":0,"topZ":192,"postSize":32}; )"
     R"({"type":"curved_corridor","center":[0,0,0],"innerRadius":128,)"
     R"("outerRadius":256,"startAngle":0,"turnDegrees":90,"height":128,)"
     R"("segments":8,"wallThickness":16,"caps":"both"}; )"
     R"({"type":"arc_ramp","center":[0,0,0],"radius":256,"width":128,)"
     R"("startAngle":0,"turnDegrees":180,"rise":128,"segments":24,) "
     R"("thickness":16}; )"
     R"({"type":"ramp_between","start":[0,0,0],"end":[256,0,64],)"
     R"("width":128,"thickness":16}; )"
     R"({"type":"wedge","min":[0,0,0],"max":[256,128,64],"axis":"x"}; )"
     R"({"type":"ramp","min":[0,0,0],"max":[256,128,64],"axis":"x"}.)"},
    {"properties",
     QJsonObject{
       {"type",
        stringProperty("Operation type. Primitive modeling types: box, cylinder, prism, "
                       "polyhedron, cylinder_sector, path_ribbon, repeat_translate, "
                       "repeat_grid, stepped_mass, support_posts_between, arc_ramp, "
                       "helical_ramp, ramp_between, and wedge. curved_corridor and "
                       "legacy ramp remain for compatibility; scene-shell, opening, "
                       "obstacle, and straight-step composition should use skill "
                       "recipes/IR or explicit primitive operations. Prefer "
                       "arc_ramp/helical_ramp or ramp_between over terraced "
                       "curved_corridor/legacy ramp for route/surf/slide semantics.")},
       {"min", vec3Property("Minimum corner for box-like operations.")},
       {"max", vec3Property("Maximum corner for box-like operations.")},
       {"start",
        vec3Property("Route start point for ramp_between. The ramp rises or falls along "
                     "start -> end.")},
       {"end",
        vec3Property(
          "Route end point for ramp_between. Use end.z > start.z for an uphill ramp.")},
       {"material", stringProperty("Per-operation material override.")},
       {"metadata", genericMetadataSchema()},
       {"parts",
        arrayProperty(
          "Optional part names to generate for part-aware operations. curved_corridor "
          "supports floor, ceiling, inner_wall, outer_wall, start_cap, end_cap; "
          "path_ribbon supports surface/floor/ribbon, preserves explicit floor "
          "metadata, and defaults unspecified output to surface.")},
       {"partMaterials",
        QJsonObject{
          {"type", "object"},
          {"description", "Optional per-part material map, e.g. {floor:'clip'}."},
          {"additionalProperties", QJsonObject{{"type", "string"}}},
        }},
       {"partMetadata",
        QJsonObject{
          {"type", "object"},
          {"description", "Optional per-part metadata map keyed by part name."},
          {"additionalProperties", genericMetadataSchema()},
        }},
       {"points2d", arrayProperty("Convex prism footprint points as [x,y].")},
       {"points3d",
        arrayProperty(
          "Optional path_ribbon centerline points as [x,y,z]. When present, each "
          "ribbon segment follows the lower endpoint Z plus zOffset; it does not "
          "interpolate a continuous ramp surface between different Z values.")},
       {"points", arrayProperty("Convex polyhedron points as [x,y,z].")},
       {"minZ", numberProperty("Minimum Z for prism or cylinder_sector.")},
       {"maxZ", numberProperty("Maximum Z for prism or cylinder_sector.")},
       {"width",
        numberProperty("Path ribbon width, or ramp_between width, in map units.")},
       {"zOffset", numberProperty("Vertical offset for points3d path_ribbon.")},
       {"miterLimit",
        numberProperty(
          "Maximum path_ribbon corner miter length as a half-width multiple.")},
       {"center", vec3Property("Center for circular operations.")},
       {"innerRadius",
        numberProperty("Inner radius for cylinder_sector/curved_corridor.")},
       {"outerRadius",
        numberProperty("Outer radius for cylinder_sector/curved_corridor.")},
       {"startAngle", numberProperty("Start angle in degrees.")},
       {"endAngle", numberProperty("End angle in degrees for cylinder_sector.")},
       {"turnDegrees",
        numberProperty(
          "Arc sweep in degrees for curved_corridor, arc_ramp, or helical_ramp. "
          "Must be finite, non-zero, and at most 360 degrees.")},
       {"radius", numberProperty("Centerline radius for arc_ramp/helical_ramp.")},
       {"rise", numberProperty("Total Z change for arc_ramp/helical_ramp.")},
       {"orderStart",
        numberProperty("Starting metadata order for arc_ramp/helical_ramp segments.")},
       {"orderStep",
        numberProperty("Metadata order increment for arc_ramp/helical_ramp segments.")},
       {"height", numberProperty("Height for curved_corridor.")},
       {"slopeStartZ",
        numberProperty(
          "Starting Z offset for terraced curved_corridor; this does not create "
          "continuous sloped top faces.")},
       {"slopeEndZ",
        numberProperty(
          "Ending Z offset for terraced curved_corridor; use arc_ramp/helical_ramp "
          "for true sloped curved surfaces.")},
       {"segments",
        integerProperty("Segment count for curved_corridor or arc_ramp/helical_ramp.")},
       {"wallThickness",
        numberProperty("Inner/outer wall thickness for curved_corridor wall parts.")},
       {"floorThickness",
        numberProperty("Floor slab thickness for curved_corridor floor parts.")},
       {"ceilingThickness",
        numberProperty("Ceiling slab thickness for curved_corridor ceiling parts.")},
       {"caps", stringProperty("curved_corridor caps: none, start, end, or both.")},
       {"levels", integerProperty("stepped_mass level count, 1..256.")},
       {"inset", numberProperty("stepped_mass xy inset per level.")},
       {"stepHeight", numberProperty("stepped_mass height per level.")},
       {"bottomZ", numberProperty("support_posts_between bottom Z.")},
       {"topZ", numberProperty("support_posts_between top Z.")},
       {"postSize", numberProperty("support_posts_between square post width/depth.")},
       {"steps", integerProperty("Stair step count.")},
       {"axis",
        stringProperty("Axis for cylinder/wedge/legacy ramp: x, y, or z. Legacy ramp "
                       "has weak route semantics; prefer ramp_between start/end.")},
       {"sides", integerProperty("Cylinder side count, clamped to 3..128.")},
       {"thickness", numberProperty("Brush thickness for ramp/path primitives.")},
       {"snapMode",
        stringProperty(
          "Circular vertex snap mode for cylinder/cylinder_sector/curved_corridor: "
          "grid, radial, or none. Batch cylinder defaults to grid so generated "
          "whitebox vertices stay integer/grid-safe.")},
       {"count", integerProperty("repeat_translate repetition count, 1..256.")},
       {"counts",
        arrayProperty(
          "repeat_grid repetition counts: either one integer or one to three integers; "
          "total instances must be <= 4096.")},
       {"offset",
        vec3Property(
          "repeat_translate offset added for each repetition; must be non-zero when "
          "count is greater than one.")},
       {"offsets",
        arrayProperty(
          "repeat_grid offset vector for one-axis shorthand, or offset vectors per axis; "
          "length must match counts.")},
       {"operation",
        QJsonObject{
          {"type", "object"},
          {"description", "repeat_translate or repeat_grid child operation object."},
          {"additionalProperties", true},
        }},
     }},
    {"required", QJsonArray{"type"}},
    {"additionalProperties", true},
  };
}

QJsonObject boxBatchItemSchema()
{
  return objectSchema(
    {
      {"min", vec3Property("Box minimum corner in map units.")},
      {"max", vec3Property("Box maximum corner in map units.")},
      {"material", stringProperty("Optional per-box material override.")},
    },
    {"min", "max"});
}

QJsonObject checkedEntityBatchItemSchema()
{
  return objectSchema(
    {
      {"classname", stringProperty("Point entity classname to validate.")},
      {"origin", vec3Property("Entity origin in map units.")},
      {"properties", stringObjectProperty("Properties to add or update.")},
    },
    {"classname"});
}

bool isLongRunningTool(const McpToolDefinition& tool)
{
  return tool.name.startsWith("ir_") || tool.name.startsWith("heightmap_")
         || tool.name.startsWith("python_") || tool.name.contains("review")
         || tool.name == "compile_run";
}

bool isFastTool(const McpToolDefinition& tool)
{
  return !tool.mutatesDocument
         && (tool.category == "core" || tool.name.endsWith("_list")
             || tool.name.endsWith("_get") || tool.name.endsWith("_search")
             || tool.name.endsWith("_inspect") || tool.name.endsWith("_status")
             || tool.name == "entity_schema");
}

} // namespace

const std::vector<McpToolDefinition>& defaultToolCatalog()
{
  static const auto Catalog = std::vector<McpToolDefinition>{
    {
      "tb_inspect",
      "Inspect bounded TrenchBroom process, document, map, selection, and action state.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"view",
         stringProperty(
           "status, document, documents, map, selection, objects, entities, materials, "
           "assets, modules, viewport, actions, or compile. Defaults to status.")},
        {"query", stringProperty("Optional material or asset text query.")},
        {"type", stringProperty("Optional object type or asset type filter.")},
        {"limit", integerProperty("Maximum object, material, or asset results.")},
        {"detail", stringProperty("summary or full object detail.")},
      }),
      "core",
    },
    {
      "tb_api",
      "Search the public trenchbroom Python API or inspect one exact API symbol.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional case-insensitive API keyword search.")},
        {"symbol", stringProperty("Optional exact symbol, for example Document.path.")},
      }),
      "python",
    },
    {
      "tb_execute_python",
      "Run trusted Python against one guarded map document in transaction or action "
      "mode.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"executionId",
           stringProperty("Caller-generated id used for execution recovery.")},
          {"code",
           stringProperty("Inline Python source. Provide exactly one of code or path.")},
          {"path",
           stringProperty(
             "Absolute Python script path. Provide exactly one of code or path.")},
          {"arguments",
           withDescription(
             QJsonObject{{"type", "object"}, {"additionalProperties", true}},
             "JSON object exposed to the script as arguments.")},
          {"document",
           withDescription(
             QJsonObject{
               {"type", "object"},
               {"additionalProperties", false},
               {"properties",
                QJsonObject{
                  {"fingerprint",
                   stringProperty("Document fingerprint from tb_inspect.")},
                  {"path", stringProperty("Saved document path from tb_inspect.")}}}},
             "Target document identity. Transaction mode requires fingerprint.")},
          {"mode",
           stringProperty(
             "transaction (default) or action for non-atomic UI/file actions.")},
          {"name", stringProperty("Native transaction name. Defaults to MCP Python.")},
          {"timeoutMs",
           integerProperty(
             "Cooperative Python budget from 1 to 90000 ms. Defaults to 30000.")},
        },
        {"executionId"}),
      "python",
    },
    {
      "tb_history",
      "Inspect MCP/native operation history, or undo and redo in Edit mode.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"action",
         stringProperty("status, list, inspect, undo, or redo. Defaults to status.")},
        {"operationId", stringProperty("Operation id for inspect when required.")},
      }),
      "operation",
    },
    {
      "tb_validate",
      "Validate the map, problems, slopes, route continuity, or shell seams.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"action",
         stringProperty(
           "map, problems, slopes, route, or shell_seams. Defaults to map.")},
        {"objectIds",
         arrayProperty("Optional object targets for the selected validation.")},
        {"detail", summaryFullDetailProperty()},
      }),
      "geometry",
    },
    {
      "tb_capture",
      "Capture the current viewport or bounded review evidence without mutating the map.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"action",
         stringProperty(
           "current, 2d, 3d, scene, targets, operation, selector, or module. Defaults to "
           "current.")},
        {"objectIds", arrayProperty("Optional object targets for scene review.")},
        {"edgeMode", stringProperty("all or silhouette for scene review.")},
      }),
      "viewport",
    },
    {
      "tb_status",
      "Return TrenchBroom MCP bridge status and active document summary.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "tb_doctor",
      "Diagnose MCP bridge configuration, mode, local trust, and tool availability.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"detail",
         stringProperty(
           "summary or full. Summary omits full tool lists/diagnostics to keep status "
           "checks compact.")},
      }),
    },
    {
      "documents_list",
      "List documents currently opened in TrenchBroom.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "documents_open",
      "Open a map document by absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute path to the map document.")},
        },
        {"path"}),
    },
    {
      "documents_open_verified",
      "Open, activate, and verify a map document before returning success.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute path to the map document.")},
          {"waitMs",
           integerProperty(
             "Maximum verification wait in milliseconds. Defaults to 5000.")},
          {"activate",
           boolProperty(
             "Activate the opened document before verification. Defaults to true.")},
        },
        {"path"}),
    },
    {
      "documents_activate",
      "Activate an open document by document index or path.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Document index from documents_list.")},
        {"path", stringProperty("Document path from documents_list.")},
      }),
    },
    {
      "documents_save",
      "Save an open document. Transient documents require an absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Optional document index from documents_list.")},
        {"path", stringProperty("Optional absolute save path.")},
      }),
    },
    {
      "documents_save_current",
      "Save the active document. Alias of documents_save for automated MCP flows; "
      "use expectedDocumentPath to guard the active map before saving.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"expectedDocumentPath",
         stringProperty("Optional active document guard before saving.")},
      }),
    },
    {
      "documents_save_as",
      "Save the active document to an absolute path. Alias of documents_save with "
      "path and optional expectedDocumentPath guard.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute save path.")},
          {"expectedDocumentPath",
           stringProperty("Optional active document guard before saving.")},
        },
        {"path"}),
    },
    {
      "documents_close",
      "Close an open document. Dirty documents require discardChanges=true.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Optional document index from documents_list.")},
        {"discardChanges", boolProperty("Allow closing a modified document.")},
      }),
    },
    {
      "documents_export",
      "Export an open document to a map file by absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"index", integerProperty("Optional document index from documents_list.")},
          {"path", stringProperty("Absolute export path.")},
          {"stripTbProperties",
           boolProperty("Strip TrenchBroom-specific properties from exported map.")},
        },
        {"path"}),
    },
    {
      "document_snapshot",
      "Return metadata for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "map_snapshot",
      "Return a compact map summary for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "map_search",
      "Search entities, brushes, and properties in the active map.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"query", stringProperty("Text, classname, targetname, or property query.")},
        },
        {"query"}),
    },
    {
      "selection_get",
      "Return the current editor selection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "selection_inspect",
      "Inspect the current editor selection with compact summaries by default. Use "
      "detail=full or includeProperties=true to read selected entity key/value "
      "properties such as targetname, target, origin, angles, and custom fields.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"detail",
         stringProperty(
           "summary or full. Defaults to summary; full expands entity properties, "
           "brush faces, and child summaries.")},
        {"includeProperties",
         boolProperty(
           "Include complete entity key/value properties. Defaults to true when "
           "detail=full, otherwise false.")},
        {"includeChildren",
         boolProperty(
           "Include direct child object summaries for selected groups or containers. "
           "Defaults to false.")},
      }),
    },
    {
      "entity_link_chain_inspect",
      "Inspect entity key/value links in the active map without reading the .map file. "
      "Defaults to the current selected entity as the start and follows targetname -> "
      "target style links. Reports missing links, duplicate names, and cycles instead "
      "of guessing.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"classname",
         stringProperty(
           "Optional entity classname filter such as path_corner. When set, the "
           "start and resolved nodes must match it.")},
        {"start",
         withDescription(
           QJsonObject{{"type", "object"}},
           "Start descriptor. Use {source:'selection'} or "
           "{source:'targetname', targetname:'name'}. Defaults to selection.")},
        {"nameKey",
         stringProperty(
           "Entity property used as the node name. Defaults to targetname.")},
        {"nextKey",
         stringProperty(
           "Entity property used as the outgoing link. Defaults to target.")},
        {"detail",
         stringProperty(
           "summary or full. Defaults to summary; full includes complete properties.")},
        {"includeAllNodes",
         boolProperty(
           "Include a compact list of all candidate nodes and duplicate names. "
           "Defaults to false.")},
      }),
    },
    {
      "selection_set",
      "Set the current editor selection using MCP object ids.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to select.")},
        },
        {"objectIds"}),
    },
    {
      "selection_filter",
      "Filter map objects by type, classname, targetname, material, bounds, or text.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"type",
         stringProperty(
           "Optional node type: world, layer, group, entity, brush, or patch.")},
        {"classname", stringProperty("Optional entity classname.")},
        {"targetname", stringProperty("Optional entity targetname.")},
        {"material", stringProperty("Optional brush material name.")},
        {"query", stringProperty("Optional text query.")},
        {"min", vec3Property("Optional bounds minimum corner.")},
        {"max", vec3Property("Optional bounds maximum corner.")},
        {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
        {"select",
         boolProperty("Replace current selection with matching selectable nodes.")},
        {"excludeWorld",
         boolProperty("Exclude node:world from returned objectIds. Defaults to true.")},
        {"selectableOnly",
         boolProperty(
           "Return only objects that can be selected directly. Defaults to false.")},
        {"leafOnly",
         boolProperty(
           "Return only leaf nodes with no children, useful before delete/transform.")},
        {"exactTypeOnly",
         boolProperty(
           "Require exact node type matching when type is provided. Defaults to true.")},
        {"limit", integerProperty("Maximum result count, defaults to 100.")},
      }),
    },
    {
      "selection_by_bounds",
      "Select leaf selectable objects whose logical bounds intersect or fit inside a "
      "box. Defaults exclude world/container matches.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"min", vec3Property("Bounds minimum corner.")},
          {"max", vec3Property("Bounds maximum corner.")},
          {"mode", stringProperty("Bounds mode: intersects or contains.")},
          {"excludeWorld",
           boolProperty("Exclude node:world. Defaults to true for bounds queries.")},
          {"selectableOnly",
           boolProperty(
             "Return only directly selectable nodes. Defaults to true for bounds "
             "queries.")},
          {"leafOnly",
           boolProperty("Return only leaf nodes. Defaults to true for bounds queries.")},
          {"exactTypeOnly",
           boolProperty("Require exact type matches when type is supplied.")},
          {"detail", summaryFullDetailProperty()},
        },
        {"min", "max"}),
    },
    {
      "selection_grow",
      "Grow the current node selection to parents, children, or siblings.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"mode", stringProperty("Growth mode: parents, children, or siblings.")},
      }),
    },
    {
      "viewport_focus",
      "Retired lightweight-runtime viewport helper. For normal Agent review, use "
      "render_review_current_scene(scope:\"selection\"), render_review_selector, or "
      "module_render_review instead.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional MCP object ids to focus.")},
      }),
    },
    {
      "viewport_clear_marks",
      "Clear MCP overlay markers, optionally clearing the editor selection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"clearSelection", boolProperty("Also clear the current editor selection.")},
      }),
    },
    {
      "viewport_layout_get",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use geometry review tools instead of live UI layout inspection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "viewport_layout_set",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "geometry review tools now provide deterministic saved review bundles without "
      "changing the live UI layout.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"layout",
           stringProperty(
             "Target layout: onePane, twoPanes, threePanes, fourPanes, or 1/2/3/4.")},
        },
        {"layout"}),
    },
    {
      "viewport_camera_frame_bounds",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use render_review_selector or module_render_review view presets instead of "
      "controlling the live editor camera.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds",
         arrayProperty("Optional object ids whose combined bounds to frame.")},
        {"bounds",
         objectSchema(
           {
             {"min", vec3Property("Bounds minimum corner.")},
             {"max", vec3Property("Bounds maximum corner.")},
           },
           {"min", "max"})},
        {"min", vec3Property("Alternative bounds minimum corner.")},
        {"max", vec3Property("Alternative bounds maximum corner.")},
        {"azimuth",
         numberProperty("Horizontal orbit angle in degrees around +Z. Defaults to -45.")},
        {"elevation",
         numberProperty(
           "Vertical orbit angle in degrees, clamped to [-85,85]. Defaults to 32.")},
        {"distanceScale",
         numberProperty(
           "Camera distance as a bounds diagonal multiplier. Defaults to 1.35.")},
        {"minDistance",
         numberProperty("Minimum camera distance in map units. Defaults to 256.")},
        {"targetOffset",
         vec3Property("Optional offset added to the computed bounds center target.")},
      }),
    },
    {
      "viewport_camera_set",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use render_review_selector or module_render_review view presets instead of "
      "controlling the live editor camera.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"position", vec3Property("Camera position in map units.")},
          {"target", vec3Property("Point the camera should look at in map units.")},
          {"up", vec3Property("Optional camera up vector. Defaults to [0,0,1].")},
          {"zoom", numberProperty("Optional camera zoom. Defaults to 1.")},
        },
        {"position", "target"}),
    },
    {
      "actions_list",
      "List executable TrenchBroom actions for the current context.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "action_execute",
      "Execute a TrenchBroom action by id if it is enabled in the current context.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"actionId", stringProperty("Action identifier from actions_list.")},
        },
        {"actionId"}),
    },
    {
      "overlay_set",
      "Set MCP overlay labels or highlighted object ids.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"highlightObjectIds",
         arrayProperty("Object ids to highlight by logical bounds.")},
        {"labels",
         arrayProperty(
           "Label markers. Each item may contain text plus objectId or position.")},
        {"pointMarkers",
         arrayProperty("Point markers with position and optional label.")},
        {"boundsMarkers",
         arrayProperty("Bounds markers with min/max and optional label.")},
      }),
    },
    {
      "overlay_clear",
      "Clear MCP overlay state.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "viewport_capture_current",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use render_review_current_scene, render_review_selector, or "
      "render_review_operation for Agent scene review.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
    },
    {
      "viewport_capture_3d",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use render_review_selector, render_review_operation, or module_render_review.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
    },
    {
      "viewport_capture_2d",
      "Retired lightweight-runtime viewport helper. Searchable for migration only; "
      "use render_review_selector, render_review_operation, or module_render_review.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
    },
    {
      "viewport_capture_scene_review",
      "Retired lightweight-runtime viewport helper. Searchable for migration only. "
      "For normal Agent self-review, use geometry review tools such as "
      "render_review_selector, render_review_operation, render_review_current_scene, "
      "or module_render_review.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"sceneName", stringProperty("Optional scene or checkpoint label.")},
        {"objectIds",
         arrayProperty("Optional object ids to select, focus, and highlight.")},
        {"operationIds",
         arrayProperty(
           "Optional MCP operation ids. Live operation objects are used for focus, "
           "highlight, and framing bounds.")},
        {"framing",
         stringProperty(
           "Camera framing preset when no explicit camera is supplied: current, "
           "overview_orbit, top_fit, side_profile, or route_follow. Defaults to "
           "current.")},
        {"bounds",
         objectSchema(
           {
             {"min", vec3Property("Framing bounds minimum corner.")},
             {"max", vec3Property("Framing bounds maximum corner.")},
           },
           {"min", "max"})},
        {"views",
         arrayProperty(
           "Views to capture: current/window, 3d, 2d. Defaults to current, 3d, 2d.")},
        {"layout",
         stringProperty(
           "Optional layout to switch to before capture: twoPanes, threePanes, or "
           "fourPanes are useful when 2D and 3D screenshots are both required.")},
        {"camera",
         objectSchema({
           {"bounds",
            objectSchema(
              {
                {"min", vec3Property("Bounds minimum corner.")},
                {"max", vec3Property("Bounds maximum corner.")},
              },
              {"min", "max"})},
           {"min", vec3Property("Alternative bounds minimum corner.")},
           {"max", vec3Property("Alternative bounds maximum corner.")},
           {"azimuth", numberProperty("3D orbit azimuth in degrees.")},
           {"elevation", numberProperty("3D orbit elevation in degrees.")},
           {"distanceScale", numberProperty("3D camera distance scale.")},
           {"minDistance", numberProperty("3D minimum camera distance.")},
           {"targetOffset", vec3Property("Optional 3D target offset.")},
           {"position", vec3Property("Explicit 3D camera position.")},
           {"target", vec3Property("Explicit 3D camera look-at target.")},
           {"up", vec3Property("Optional explicit 3D camera up vector.")},
           {"zoom", numberProperty("Optional explicit 3D camera zoom.")},
         })},
        {"checklist",
         arrayProperty(
           "Optional review checklist strings. Defaults to whitebox scene checks.")},
        {"returnBase64",
         boolProperty("Return PNG data as base64. Defaults to false to save context.")},
        {"isolate",
         boolProperty(
           "Temporarily isolate the review target for capture without committing map "
           "visibility or undo changes. Current implementation frames/highlights the "
           "target and writes a focused review bundle. Defaults to false.")},
        {"isolateMode",
         stringProperty(
           "Requested isolation mode: hide_others, fade_others, or highlight_only. "
           "hide_others/fade_others currently fall back to safe highlight_only capture "
           "until renderer-level target filtering is available.")},
        {"min2dHeight",
         integerProperty(
           "Minimum acceptable 2D capture height in pixels before warning/retry. "
           "Defaults to 360.")},
        {"highlight",
         boolProperty(
           "Highlight focused object ids and add a scene label. Defaults to true.")},
        {"clearSelectionBeforeCapture",
         boolProperty(
           "After focusing objectIds, clear the editor selection before capturing to "
           "reduce wireframe clutter. Defaults to false.")},
        {"clearSelectionAfter",
         boolProperty("Clear selection and overlay after capture. Defaults to false.")},
      }),
    },
    {
      "render_review_targets",
      "Low-level geometry review renderer used by higher-level review tools. Prefer "
      "render_review_selector, render_review_operation, or render_review_current_scene "
      "unless you already have explicit objectIds/operationIds and need direct "
      "control.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationIds",
         arrayProperty(
           "MCP operation ids whose live changed objects are the review target.")},
        {"objectIds", arrayProperty("Explicit MCP object ids to review.")},
        {"bounds",
         objectSchema(
           {
             {"min", vec3Property("Fallback target bounds minimum corner.")},
             {"max", vec3Property("Fallback target bounds maximum corner.")},
           },
           {"min", "max"})},
        {"views",
         arrayProperty(
           "Geometry review views. Defaults to iso_overview_ne, iso_overview_sw, "
           "top_plan, side_elevation_long, and front_elevation_cross. Legacy "
           "overview_3d/detail_3d/top_2d_fit/side_2d_fit names are accepted.")},
        {"style",
         stringProperty(
           "Rendering style: whitebox_edges, material_tint_edges, or "
           "height_heatmap_edges. material_tint_edges assigns stable semantic colors "
           "from face material names; height_heatmap_edges colors by Z height and "
           "softens dense terrain edges.")},
        {"preset",
         stringProperty(
           "Optional default bundle preset. route_platform favors strong whitebox "
           "edges, dual iso views, top plan, side elevation, and modest vertical "
           "exaggeration for sparse platform chains.")},
        {"verticalExaggeration",
         numberProperty(
           "Scale Z distances around the target minimum Z before rendering. Defaults "
           "to 1.0; route_platform defaults to 1.6.")},
        {"edgeMode",
         stringProperty(
           "Edge drawing mode: auto, all, minimal, silhouette, or none. silhouette "
           "extracts only the projected coverage boundary. For terrain review, "
           "height_heatmap_edges defaults to minimal so same-height grid lines do not "
           "overwhelm the image; use none for clean color-only height maps.")},
        {"combineViews",
         boolProperty(
           "Write a combined contact_sheet.png for context-efficient Agent vision. "
           "Defaults to true.")},
        {"contactSheetSize",
         arrayProperty("Optional [width,height] for the combined contact sheet.")},
        {"contactSheetMaxCaptures",
         integerProperty(
           "Maximum number of source views included in contact_sheet.png. Defaults to "
           "2 so each panel stays readable for Agent vision; all individual view PNGs "
           "are still "
           "written.")},
        {"imageSize",
         arrayProperty(
           "Optional [width,height] in pixels. Values are clamped to production review "
           "limits; default is 1400x1000.")},
        {"imageWidth", integerProperty("Optional image width override.")},
        {"imageHeight", integerProperty("Optional image height override.")},
        {"outputDir",
         stringProperty(
           "Optional root directory for review bundles. Captures are written under "
           "<outputDir>/<reviewId>.")},
        {"includeAxes", boolProperty("Draw a small orientation axis marker.")},
        {"includeBoundsBox",
         boolProperty("Draw the target bounding box as a translucent dashed outline.")},
        {"includeEntityLabels",
         boolProperty(
           "Draw classname labels for point/entity placeholders. Defaults to true; "
           "dense targets auto-hide text labels while keeping entity glyph markers.")},
        {"includeOrderLabels",
         boolProperty(
           "Draw ordered target labels. route_platform enables this by default.")},
        {"includeDirectionLabels",
         boolProperty(
           "Draw arrows between ordered targets. route_platform enables this by "
           "default.")},
        {"labelStride",
         integerProperty(
           "Draw only every Nth order label when includeOrderLabels is true. Defaults "
           "to 1.")},
        {"labelParts",
         arrayProperty(
           "Optional metadata part names to label, such as road, rail, support, or "
           "spawn. Labels are auto-hidden on dense targets.")},
        {"autoHideLabelsThreshold",
         integerProperty(
           "Automatically hide dense order/entity/part labels when targetObjectCount "
           "exceeds this threshold. Defaults to 120; use 0 to disable.")},
        {"maxDetailedFaces",
         integerProperty(
           "Maximum brush faces rendered as real polygons before falling back to bounds "
           "geometry. Defaults to 20000.")},
        {"detail",
         stringProperty(
           "Return detail level. Summary is default; full may include more diagnostics "
           "in later versions.")},
      }),
    },
    {
      "render_review_current_scene",
      "Create a compact Agent-readable geometry review for the active document in one "
      "call. The tool automatically reviews current brush geometry, chooses terrain-"
      "friendly defaults when appropriate, writes a contact_sheet.png, and returns a "
      "small summary with preferredCapturePath. Use detail=full for captures, quality "
      "arrays, and other manifest details. renderReadable only checks render output "
      "readability; qualityValid is a legacy alias, not semantic geometry validation. "
      "Human or skill review must judge whether the image matches the requested scene "
      "intent.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"scope",
         stringProperty(
           "Review scope: all, mcp_history, or selection. Defaults to all for "
           "compatibility; mcp_history is recommended for isolated generated-scene "
           "review.")},
        {"preset",
         stringProperty(
           "Optional preset: auto, terrain, terrain_route, route_platform, building, "
           "material, or whitebox. route_platform uses strong whitebox edges and "
           "modest vertical exaggeration for platform/route chains. Defaults to auto.")},
        {"style",
         stringProperty(
           "Optional rendering style override: whitebox_edges, material_tint_edges, or "
           "height_heatmap_edges.")},
        {"verticalExaggeration",
         numberProperty(
           "Scale Z distances around the target minimum Z before rendering. Defaults "
           "to 1.0; route_platform defaults to 1.6.")},
        {"edgeMode",
         stringProperty(
           "Optional edge drawing override: auto, all, minimal, silhouette, or none. "
           "Dense terrain defaults to none for a clean first-pass image.")},
        {"views",
         arrayProperty(
           "Optional views. Defaults to iso_overview_ne, top_plan, side_elevation_long, "
           "and front_elevation_cross.")},
        {"combineViews",
         boolProperty(
           "Write a combined contact_sheet.png and return it as preferredCapturePath. "
           "Defaults to true.")},
        {"contactSheetSize",
         arrayProperty("Optional [width,height] for the combined contact sheet.")},
        {"contactSheetMaxCaptures",
         integerProperty(
           "Maximum number of source views included in contact_sheet.png. Defaults to "
           "2 so each panel stays readable for Agent vision; all individual view PNGs "
           "are still "
           "written.")},
        {"imageSize", arrayProperty("Optional [width,height] for source views.")},
        {"outputDir",
         stringProperty(
           "Optional root directory for review bundles. Captures are written under "
           "<outputDir>/<reviewId>.")},
        {"includeAxes", boolProperty("Draw a small orientation axis marker.")},
        {"includeBoundsBox",
         boolProperty("Draw the target bounding box as a translucent dashed outline.")},
        {"includeEntityLabels",
         boolProperty(
           "Draw classname labels for point/entity placeholders. Defaults to true; "
           "dense targets auto-hide text labels while keeping entity glyph markers.")},
        {"includeOrderLabels",
         boolProperty(
           "Draw ordered target labels. route_platform enables this by default.")},
        {"includeDirectionLabels",
         boolProperty(
           "Draw arrows between ordered targets. route_platform enables this by "
           "default.")},
        {"labelStride",
         integerProperty(
           "Draw only every Nth order label when includeOrderLabels is true. Defaults "
           "to 1.")},
        {"labelParts",
         arrayProperty(
           "Optional metadata part names to label, such as road, rail, support, or "
           "spawn. Labels are auto-hidden on dense targets.")},
        {"autoHideLabelsThreshold",
         integerProperty(
           "Automatically hide dense order/entity/part labels when targetObjectCount "
           "exceeds this threshold. Defaults to 120; use 0 to disable.")},
        {"maxDetailedFaces",
         integerProperty(
           "Maximum brush faces rendered as real polygons before falling back to bounds "
           "geometry. Defaults to 20000.")},
        {"idsMode",
         stringProperty(
           "Object id verbosity for tools that resolve targets: none, count, sample, or "
           "full. Current-scene review mainly reports counts.")},
        {"detail",
         stringProperty(
           "summary or full. Summary is default and suppresses large arrays from the "
           "MCP response; full details are still written to manifest.json.")},
      }),
    },
    {
      "render_review_operation",
      "Create an isolated Agent-readable geometry review bundle for generated scene "
      "objects. Pass operationIds or objectIds; the CPU renderer draws only target "
      "geometry with whitebox faces and strong outlines, avoiding live viewport "
      "layout/visibility changes. renderReadable only checks render output readability; "
      "qualityValid is a legacy alias, not semantic geometry validation. Human or skill "
      "review must judge whether the image matches the requested scene intent.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationIds",
         arrayProperty(
           "MCP operation ids whose live changed objects are the review target.")},
        {"objectIds", arrayProperty("Explicit MCP object ids to review.")},
        {"bounds",
         objectSchema(
           {
             {"min", vec3Property("Fallback target bounds minimum corner.")},
             {"max", vec3Property("Fallback target bounds maximum corner.")},
           },
           {"min", "max"})},
        {"views",
         arrayProperty(
           "Review views. Defaults to iso_overview_ne, iso_overview_sw, top_plan, "
           "side_elevation_long, and front_elevation_cross. Legacy viewport review "
           "view names are accepted.")},
        {"style",
         stringProperty("Rendering style: whitebox_edges, material_tint_edges, or "
                        "height_heatmap_edges.")},
        {"preset",
         stringProperty(
           "Optional default bundle preset. route_platform favors strong whitebox "
           "edges, dual iso views, top plan, side elevation, and modest vertical "
           "exaggeration for sparse platform chains.")},
        {"verticalExaggeration",
         numberProperty(
           "Scale Z distances around the target minimum Z before rendering. Defaults "
           "to 1.0; route_platform defaults to 1.6.")},
        {"edgeMode",
         stringProperty(
           "Edge drawing mode: auto, all, minimal, silhouette, or none. Use silhouette "
           "for the projected outer shape and none/minimal for dense terrain.")},
        {"combineViews",
         boolProperty(
           "Write a combined contact_sheet.png and return it as preferredCapturePath. "
           "Defaults to true.")},
        {"contactSheetSize",
         arrayProperty("Optional [width,height] for the combined contact sheet.")},
        {"contactSheetMaxCaptures",
         integerProperty(
           "Maximum number of source views included in contact_sheet.png. Defaults to "
           "2 so each panel stays readable for Agent vision; all individual view PNGs "
           "are still "
           "written.")},
        {"isolateMode",
         stringProperty(
           "Requested isolation mode: hide_others, fade_others, or highlight_only. "
           "Accepted for compatibility; geometry review always renders only targets.")},
        {"framingPreset",
         stringProperty(
           "Compatibility parameter from viewport review. Geometry review ignores "
           "camera presets and fits each orthographic view to the target.")},
        {"outputDir",
         stringProperty(
           "Optional root directory for review bundles. Captures are written under "
           "<outputDir>/<reviewId>; otherwise the MCP temp review directory is used.")},
        {"imageSize", arrayProperty("Optional [width,height] in pixels.")},
        {"includeAxes", boolProperty("Draw a small orientation axis marker.")},
        {"includeBoundsBox",
         boolProperty("Draw the target bounding box as a translucent dashed outline.")},
        {"includeEntityLabels",
         boolProperty(
           "Draw classname labels for point/entity placeholders. Defaults to true; "
           "dense targets auto-hide text labels while keeping entity glyph markers.")},
        {"includeOrderLabels",
         boolProperty(
           "Draw ordered target labels. route_platform enables this by default.")},
        {"includeDirectionLabels",
         boolProperty(
           "Draw arrows between ordered targets. route_platform enables this by "
           "default.")},
        {"labelStride",
         integerProperty(
           "Draw only every Nth order label when includeOrderLabels is true. Defaults "
           "to 1.")},
        {"labelParts",
         arrayProperty(
           "Optional metadata part names to label, such as road, rail, support, or "
           "spawn. Labels are auto-hidden on dense targets.")},
        {"autoHideLabelsThreshold",
         integerProperty(
           "Automatically hide dense order/entity/part labels when targetObjectCount "
           "exceeds this threshold. Defaults to 120; use 0 to disable.")},
        {"sceneName", stringProperty("Optional scene/review label.")},
        {"returnBase64",
         boolProperty(
           "Compatibility parameter. Geometry review writes files and returns paths to "
           "save context.")},
        {"idsMode",
         stringProperty(
           "Target id verbosity: none, count, sample, or full. Defaults to count unless "
           "detail=full/ids is requested.")},
      }),
    },
    {
      "selector_preview",
      "Preview a structured JSON selector against the active map. Selectors can combine "
      "session metadata, moduleId, operation ids, type, bounds, material, classname, "
      "targetname, and text query. idsMode=count returns counts only; idsMode=sample "
      "adds small object summaries.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"selector", selectorSchema()},
        {"idsMode", stringProperty("none, count, sample, or full. Defaults to sample.")},
        {"sampleLimit", integerProperty("Maximum sample summaries, defaults to 12.")},
        {"detail",
         stringProperty("summary or full. Defaults to summary; full expands stale target "
                        "diagnostics.")},
      }),
    },
    {
      "objects_select_by_selector",
      "Replace the current selection with objects matched by a structured JSON "
      "selector. Use this to recover generated modules without carrying long object id "
      "lists.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"selector", selectorSchema()},
        {"idsMode", stringProperty("none, count, sample, or full. Defaults to sample.")},
        {"sampleLimit", integerProperty("Maximum sample summaries, defaults to 12.")},
      }),
    },
    {
      "objects_delete_by_selector",
      "Delete live objects matched by a structured JSON selector in one transaction. "
      "This is safer than pasting a long objectIds array when the target was tagged "
      "with moduleId/part/role metadata.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"selector", selectorSchema()},
        {"transactionName", stringProperty("Optional delete transaction label.")},
      }),
    },
    {
      "render_review_selector",
      "Render an isolated geometry review for objects matched by a structured JSON "
      "selector. This composes selector_preview with render_review_targets and keeps "
      "the MCP response compact. renderReadable only checks render output readability; "
      "qualityValid is a legacy alias, not semantic geometry validation. Human or "
      "skill review must judge whether the image matches the requested scene intent.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"selector", selectorSchema()},
        {"views", arrayProperty("Optional review view names.")},
        {"preset", stringProperty("Optional review preset such as route_platform.")},
        {"style",
         stringProperty("whitebox_edges, material_tint_edges, or height_heatmap_edges.")},
        {"combineViews", boolProperty("Write contact_sheet.png. Defaults to true.")},
        {"contactSheetMaxCaptures",
         integerProperty("Maximum contact sheet panels. Defaults to 2.")},
        {"labelStride",
         integerProperty("Draw only every Nth order label when labels are enabled.")},
        {"labelParts",
         arrayProperty(
           "Optional metadata part names to label; dense targets auto-hide these "
           "labels.")},
        {"autoHideLabelsThreshold",
         integerProperty("Hide dense order/entity/part labels above this target count.")},
        {"outputDir", stringProperty("Optional review output root.")},
        {"detail", summaryFullDetailProperty()},
      }),
    },
    {
      "module_list",
      "List session-level modules discovered from metadata.moduleId and IR/module "
      "registry records. Modules are MCP-session only and are not written to the map. "
      "Defaults to live modules only so stale modules from previous documents do not "
      "pollute Agent target recovery.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"limit", integerProperty("Maximum modules returned, defaults to 100.")},
        {"includeStale",
         boolProperty("Include modules whose tracked objects are all stale in the active "
                      "document.")},
        {"includeEmpty",
         boolProperty(
           "Include modules with no tracked objects. Defaults to includeStale.")},
      }),
    },
    {
      "module_inspect",
      "Inspect one session-level module by moduleId, including live/stale object "
      "counts, operation ids, metadata, and bounds.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"moduleId", moduleIdProperty()},
          {"idsMode",
           stringProperty("none, count, sample, or full. Defaults to sample.")},
        },
        {"moduleId"}),
    },
    {
      "module_select",
      "Select all live objects in a session-level module.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"moduleId", moduleIdProperty()},
          {"idsMode",
           stringProperty("none, count, sample, or full. Defaults to sample.")},
        },
        {"moduleId"}),
    },
    {
      "module_render_review",
      "Render an isolated review bundle for a session-level module. By default the "
      "selector limit is raised to cover the full live module; pass limit only when "
      "you intentionally want a partial review. renderReadable only checks render "
      "output readability; qualityValid is a legacy alias, not semantic geometry "
      "validation. Human or skill review must judge whether the image matches the "
      "requested scene intent.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"moduleId", moduleIdProperty()},
          {"limit",
           integerProperty(
             "Optional maximum matched objects; omitted means full live module.")},
          {"views", arrayProperty("Optional review view names.")},
          {"preset", stringProperty("Optional review preset.")},
          {"style", stringProperty("Optional review style.")},
          {"labelStride",
           integerProperty("Draw only every Nth order label when labels are enabled.")},
          {"labelParts",
           arrayProperty(
             "Optional metadata part names to label; dense targets auto-hide these "
             "labels.")},
          {"autoHideLabelsThreshold",
           integerProperty(
             "Hide dense order/entity/part labels above this target count.")},
          {"outputDir", stringProperty("Optional review output root.")},
          {"detail", summaryFullDetailProperty()},
        },
        {"moduleId"}),
    },
    {
      "module_validate",
      "Validate a session-level module's live objects and optional route continuity. "
      "This reports geometry facts only; skill/domain logic decides gameplay or "
      "aesthetic quality.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"moduleId", moduleIdProperty()},
          {"checkRouteContinuity",
           boolProperty(
             "Also run geometry_analyze_route_continuity. Defaults to walkable/road/"
             "ramp/platform metadata targets; pass continuitySelector for a custom "
             "subset.")},
          {"continuitySelector", selectorSchema()},
          {"qualityPolicy", qualityPolicySchema()},
          {"start", vec3Property("Optional route start for continuity.")},
          {"end", vec3Property("Optional route end for continuity.")},
          {"routeDirection", vec3Property("Optional route direction for continuity.")},
          {"continuityMode",
           stringProperty(
             "Legacy pass-through to geometry_analyze_route_continuity. Prefer "
             "routeMode for new workflows.")},
          {"routeMode",
           stringProperty(
             "Passed through to geometry_analyze_route_continuity: continuous, "
             "stepped, jump_chain, spiral, or closed_loop.")},
          {"validationMode",
           stringProperty(
             "Alias for routeMode used by recipe/skill validation profiles.")},
          {"maxStepHeight",
           numberProperty(
             "Used with continuityMode=stepped. Maximum step height treated as "
             "semantically continuous.")},
          {"maxJumpGap",
           numberProperty(
             "Used with continuityMode=jump_gaps. Maximum jump gap treated as "
             "semantically continuous.")},
          {"orderBy",
           stringProperty(
             "Passed through to geometry_analyze_route_continuity: projection or "
             "metadataOrder. Use metadataOrder for routeId/order chains.")},
          {"closedLoop",
           boolProperty(
             "Passed through to geometry_analyze_route_continuity. When true, the "
             "last ordered route surface is checked against the first.")},
          {"detail",
           stringProperty(
             "Passed through to geometry_analyze_route_continuity. Summary is the "
             "default; full includes per-surface and per-seam evidence.")},
        },
        {"moduleId"}),
    },
    {
      "module_compact",
      "Remove stale session metadata/object references for one module. This cleans "
      "MCP session state only and does not modify the map or undo stack.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"moduleId", moduleIdProperty()},
        },
        {"moduleId"}),
    },
    {
      "entity_create",
      "Create an entity in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Entity classname.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"properties", stringObjectProperty("Entity key/value properties.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"classname"}),
    },
    {
      "entity_update",
      "Update entity key/value properties.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectId", stringProperty("MCP object id for a world or entity node.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"removeKeys", arrayProperty("Property keys to remove.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"objectId"}),
    },
    {
      "entity_properties_update",
      "Batch update entity key/value properties by objectIds or operationIds. Prefer "
      "this over deleting and recreating entities for key cleanup.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Entity object ids to update.")},
          {"operationIds",
           arrayProperty("MCP operation ids whose live entity objects are updated.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"properties"}),
    },
    {
      "entity_properties_delete",
      "Batch delete entity property keys by objectIds or operationIds.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Entity object ids to update.")},
          {"operationIds",
           arrayProperty("MCP operation ids whose live entity objects are updated.")},
          {"keys", arrayProperty("Entity property keys to remove.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"keys"}),
    },
    {
      "entity_delete",
      "Delete an entity, brush, patch, group, or layer node.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectId", stringProperty("MCP object id to delete.")},
        },
        {"objectId"}),
    },
    {
      "fgd_entities_list",
      "List entity classes from the active game entity definitions.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"type", stringProperty("Optional entity definition type: point or brush.")},
        {"query", stringProperty("Optional classname or description query.")},
        {"limit", integerProperty("Maximum result count, defaults to 200.")},
      }),
    },
    {
      "entity_schema",
      "Return FGD schema details for one entity classname.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Entity classname.")},
        },
        {"classname"}),
    },
    {
      "entity_create_from_schema",
      "Create an entity using FGD defaults plus supplied properties.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Point entity classname.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"classname"}),
    },
    {
      "entity_create_checked",
      "Create a point entity after checking the active FGD schema supports the "
      "classname. This is the preferred one-step helper over manually calling "
      "fgd_entities_list, entity_schema, and entity_create_from_schema.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Point entity classname to validate.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"classname"}),
    },
    {
      "entity_create_checked_batch",
      "Create multiple point entities after validating each classname against the "
      "active FGD schema. Use this for lights, spawns, and route markers that should "
      "land in one undoable transaction.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"entities",
           arrayProperty(
             "Entities to create: {classname, origin?, properties?}.",
             checkedEntityBatchItemSchema())},
          {"transactionName",
           stringProperty(
             "Transaction label, defaults to MCP: Create checked entities.")},
          {"select", boolProperty("Select created entities.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count; "
             "detail=ids/full remains accepted as legacy shorthand for full ids.")},
        },
        {"entities"}),
    },
    {
      "entity_tie_brushes",
      "Tie selected or specified brushes to a brush entity.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Brush entity classname.")},
          {"objectIds",
           arrayProperty("Optional brush object ids. Defaults to selection.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"classname"}),
    },
    {
      "entity_untie_brushes",
      "Move brushes out of brush entities back to the world or current parent.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"objectIds",
         arrayProperty(
           "Optional brush or brush entity object ids. Defaults to selection.")},
        {"idsMode",
         stringProperty(
           "none, count, sample, or full for changed object ids. Defaults to count.")},
      }),
    },
    {
      "brush_types_list",
      "List brush primitive types supported by MCP.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "shape_library_list",
      "List generic convex footprint grammars for route-aware polygon platforms.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "brush_create",
      "Create a brush primitive using a type parameter.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"type",
           stringProperty(
             "Primitive type: box, wedge, cylinder, cone, pipe, sphere, pyramid, "
             "tetrahedron, prism, cylinder_sector, or from_planes.")},
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Side count for circular primitives.")},
          {"rings", integerProperty("Ring count for UV sphere.")},
          {"iterations", integerProperty("Subdivision iterations for ico sphere.")},
          {"axis", stringProperty("Primitive axis: x, y, or z.")},
          {"thickness", numberProperty("Pipe wall thickness.")},
          {"planes", arrayProperty("Plane definitions for from_planes.")},
          {"points2d", arrayProperty("2D polygon points for prism.")},
          {"minZ", numberProperty("Minimum Z for prism or sector.")},
          {"maxZ", numberProperty("Maximum Z for prism or sector.")},
          {"center", vec3Property("Center point for cylinder sector.")},
          {"innerRadius", numberProperty("Inner radius for cylinder sector.")},
          {"outerRadius", numberProperty("Outer radius for cylinder sector.")},
          {"startAngle", numberProperty("Start angle in degrees.")},
          {"endAngle", numberProperty("End angle in degrees.")},
          {"snapMode",
           stringProperty(
             "XY vertex snap mode for cylinder_sector: grid, radial, or none.")},
          {"grid", numberProperty("Grid size for cylinder_sector snapping.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush or brushes.")},
        },
        {"type"}),
    },
    {
      "brush_create_box",
      "Create a box brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_boxes_batch",
      "Create many box brushes in one transaction. Prefer this for platform chains, "
      "jump blocks, and other repeated cuboids instead of many brush_create_box calls.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"boxes",
           arrayProperty(
             "Array of box objects: {\"min\":[x,y,z],\"max\":[x,y,z],"
             "\"material\":\"optional\"}.",
             boxBatchItemSchema())},
          {"name",
           stringProperty("Transaction label, defaults to MCP: Create box brush batch.")},
          {"material",
           stringProperty("Default material for boxes without a material field.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated boxes.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode", changedIdsModeProperty()},
        },
        {"boxes"}),
    },
    {
      "brush_create_polygon_batch",
      "Create many convex prism platforms from 2D polygons in one transaction. "
      "Prefer this for diamond, trapezoid, chamfered, and route-guiding platforms "
      "instead of box-only chains. Invalid batches fail before mutation and return "
      "polygonDiagnostics with failing point/edge indices for every invalid polygon.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"brushes",
           arrayProperty(
             "Array of platform objects: {points2d:[[x,y],...], minZ, maxZ, "
             "material?, metadata?}.",
             polygonBatchItemSchema())},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated platforms.")},
          {"transactionName",
           stringProperty(
             "Transaction label, defaults to MCP: Create polygon platform batch.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode", changedIdsModeProperty()},
          {"material",
           stringProperty("Default material for brushes without a material field.")},
        },
        {"brushes"}),
    },
    {
      "brush_create_wedge",
      "Create a wedge brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Ramp direction axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_cylinder",
      "Create a cylinder brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Cylinder side count, defaults to 16.")},
          {"axis", stringProperty("Cylinder axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_cone",
      "Create a cone brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Base side count, defaults to 16.")},
          {"axis", stringProperty("Cone axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_pipe",
      "Create a hollow cylinder from multiple convex brushes.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Side count, defaults to 16.")},
          {"thickness", numberProperty("Pipe wall thickness, defaults to 16.")},
          {"axis", stringProperty("Pipe axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_sphere",
      "Create a UV or ico sphere brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("UV sphere side count, defaults to 12.")},
          {"rings", integerProperty("UV sphere ring count, defaults to 6.")},
          {"iterations",
           integerProperty("Ico sphere iterations; when present uses ico.")},
          {"axis", stringProperty("UV sphere axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_pyramid",
      "Create a square pyramid brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Pyramid axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_tetrahedron",
      "Create a tetrahedron brush inside the given bounds.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Tetrahedron axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_from_planes",
      "Create one expert brush from plane point triples.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"planes", arrayProperty("Array of planes, each with three points.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"planes"}),
    },
    {
      "brush_create_prism",
      "Create one convex vertical prism from a 2D polygon and Z range.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"points2d", arrayProperty("Convex 2D polygon points as [x,y].")},
          {"minZ", numberProperty("Minimum Z in map units.")},
          {"maxZ", numberProperty("Maximum Z in map units.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"points2d", "minZ", "maxZ"}),
    },
    {
      "brush_create_cylinder_sector",
      "Create one convex annular cylinder-sector brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"center", vec3Property("Sector center; X/Y are used for the footprint.")},
          {"innerRadius", numberProperty("Inner radius in map units.")},
          {"outerRadius", numberProperty("Outer radius in map units.")},
          {"startAngle", numberProperty("Start angle in degrees.")},
          {"endAngle", numberProperty("End angle in degrees.")},
          {"minZ", numberProperty("Minimum Z in map units.")},
          {"maxZ", numberProperty("Maximum Z in map units.")},
          {"snapMode",
           stringProperty(
             "XY vertex snap mode: grid, radial, or none. Defaults to grid.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"center",
         "innerRadius",
         "outerRadius",
         "startAngle",
         "endAngle",
         "minZ",
         "maxZ"}),
    },
    {
      "brush_create_arch",
      "Create an arch brush primitive. Listed as unsupported until a stable native "
      "arch generator is exposed.",
      McpMode::Edit,
      true,
      false,
      objectSchema(),
    },
    {
      "brush_create_torus",
      "Create a torus brush primitive. Listed as unsupported because BSP brushes cannot "
      "represent it as a single convex brush.",
      McpMode::Edit,
      true,
      false,
      objectSchema(),
    },
    {
      "history_list",
      "Hidden diagnostic timeline for the active bridge session, including creation "
      "time and live/stale object counts when a document is active. Prefer "
      "history_status for normal Agent workflow and operation_inspect for a specific "
      "operation.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "history_status",
      "Return compact MCP operation history availability diagnostics for the active "
      "bridge session and document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "operation_inspect",
      "Inspect an MCP operation by operationId. Use detail=summary by default and "
      "detail=ids/full only when object ids or debug data are needed. Includes "
      "live/stale status when a document is active.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed/deleted object ids. Defaults "
             "to count; full maps to detail=ids.")},
        },
        {"operationId"}),
    },
    {
      "operation_select",
      "Hidden manual recovery helper that selects live map objects created or changed "
      "by an MCP operation. Prefer selector_preview, module_inspect, "
      "render_review_operation, or operation_inspect unless a human explicitly needs "
      "the native selection changed.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
        },
        {"operationId"}),
    },
    {
      "operation_validate",
      "Return generic validation status for an MCP operation. Defaults to compact "
      "summary diagnostics; use detail=full to include per-object diagnostics.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
          {"detail", summaryFullDetailProperty()},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed/deleted object ids. Defaults "
             "to count; full maps to detail=ids.")},
        },
        {"operationId"}),
    },
    {
      "history_undo_mcp",
      "Undo the latest MCP operation if it is still on top of the native undo stack.",
      McpMode::Edit,
      true,
      true,
      objectSchema(),
    },
    {
      "history_undo_to_operation",
      "Undo MCP operations from the latest operation back to and including the given "
      "operationId. Stops with structured diagnostics if the native undo stack no "
      "longer matches the expected MCP transaction.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty(
             "Target MCP operation id to undo through, inclusive. The operation must "
             "still be in the current bridge history.")},
        },
        {"operationId"}),
    },
    {
      "history_redo_mcp",
      "Redo the latest MCP operation undone by history_undo_mcp.",
      McpMode::Edit,
      true,
      true,
      objectSchema(),
    },
    {
      "asset_search",
      "Search GoldSrc model, sprite, and sound assets using the unified asset browser "
      "rules.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for asset path or display name.")},
        {"type", stringProperty("Optional asset type: model, sprite, or sound.")},
        {"limit", integerProperty("Maximum result count, defaults to 50.")},
      }),
    },
    {
      "asset_place_model",
      "Create a point entity for a .mdl asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Model path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname", stringProperty("Optional classname, defaults to cycler_sprite.")},
          {"property", stringProperty("Optional model property key, defaults to model.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"path"}),
    },
    {
      "asset_place_sprite",
      "Create a point entity for a .spr asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Sprite path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname", stringProperty("Optional classname, defaults to cycler_sprite.")},
          {"property",
           stringProperty("Optional sprite property key, defaults to model.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"path"}),
    },
    {
      "asset_place_sound",
      "Create an ambient_generic entity for a .wav asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Sound path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname",
           stringProperty("Optional classname, defaults to ambient_generic.")},
          {"property",
           stringProperty("Optional sound property key, defaults to message.")},
          {"select", boolProperty("Select the created entity.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"path"}),
    },
    {
      "textures_list",
      "List loaded materials in the active document. With an empty query, returns the "
      "first N materials plus currentMaterial and fallbackMaterial.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for material name or path.")},
        {"limit", integerProperty("Maximum result count, defaults to 200.")},
      }),
    },
    {
      "texture_search",
      "Search loaded materials in the active document. If no material matches, use "
      "fallbackMaterial from the result for safe blockout geometry. Returns detailed "
      "results plus materials/materialNames aliases for simple agent consumption.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for material name or path.")},
        {"limit", integerProperty("Maximum result count, defaults to 50.")},
      }),
    },
    {
      "texture_lock_get",
      "Return current texture lock and UV lock state for modeling operations.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "texture_lock_set",
      "Set texture lock and/or UV lock for subsequent modeling operations.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"textureLock",
         boolProperty("Texture alignment lock, matching Menu/Edit/Texture Lock.")},
        {"uvLock", boolProperty("UV lock, matching Menu/Edit/UV Lock.")},
      }),
    },
    {
      "texture_apply",
      "Apply a material to selected faces, target objects, operation targets, or a "
      "semantic subset such as top/bottom/side faces. Result reports whether the "
      "requested material exists in the loaded material set.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        mergeProperties(
          QJsonObject{{"material", stringProperty("Material name to apply.")}},
          mergeProperties(faceTargetSchemaProperties(), semanticFaceSchemaProperties())),
        {"material"}),
    },
    {
      "texture_apply_by_filter",
      "Apply a material to brushes matched by a safe selection_filter-style query. "
      "The tool only edits brush faces and ignores world/group/layer parents. It also "
      "accepts objectIds or operationIds as direct targets, then can narrow by "
      "faceSemantic or normal. Result reports materialExists and a fallbackMaterial "
      "when the requested material is not loaded.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        mergeProperties(
          QJsonObject{
            {"material", stringProperty("Material name to apply.")},
            {"type",
             stringProperty(
               "Optional node type filter. Defaults to brush for safe texture edits.")},
            {"classname", stringProperty("Optional entity classname filter.")},
            {"targetname", stringProperty("Optional entity targetname filter.")},
            {"query", stringProperty("Optional text query.")},
            {"min", vec3Property("Optional bounds minimum corner.")},
            {"max", vec3Property("Optional bounds maximum corner.")},
            {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
            {"limit", integerProperty("Maximum matched brush count, defaults to 100.")},
          },
          mergeProperties(faceTargetSchemaProperties(), semanticFaceSchemaProperties())),
        {"material"}),
    },
    {
      "texture_replace",
      "Replace one material with another on selected faces/brushes or the whole map. "
      "Result reports find/replace material existence.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"find", stringProperty("Material name to replace.")},
          {"replace", stringProperty("Replacement material name.")},
          {"scope", stringProperty("Replacement scope: selection or map.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"find", "replace"}),
    },
    {
      "texture_align_face",
      "Apply a basic UV alignment mode to a face or face selection.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        mergeProperties(
          QJsonObject{
            {"mode", stringProperty("Alignment mode: reset, paraxial, or parallel.")},
          },
          mergeProperties(faceTargetSchemaProperties(), semanticFaceSchemaProperties())),
        {"mode"}),
    },
    {
      "texture_copy_from_face",
      "Copy material and UV attributes from one face to selected or specified target "
      "faces.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        mergeProperties(
          QJsonObject{
            {"sourceObjectId", stringProperty("Source brush object id.")},
            {"sourceFaceIndex", integerProperty("Source brush face index.")},
          },
          mergeProperties(faceTargetSchemaProperties(), semanticFaceSchemaProperties())),
        {"sourceObjectId", "sourceFaceIndex"}),
    },
    {
      "face_list",
      "List faces for one brush, selected brushes, or all selected brush faces.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectId", stringProperty("Optional single brush object id.")},
        {"objectIds",
         arrayProperty("Optional object ids whose child brush faces should be listed.")},
        {"operationId", stringProperty("Optional MCP operation id target.")},
        {"operationIds", arrayProperty("Optional MCP operation id targets.")},
        {"faceSemantic", stringProperty("Optional semantic face filter.")},
        {"normal", vec3Property("Optional normal vector face filter.")},
        {"normalTolerance", numberProperty("Normal matching tolerance.")},
        {"limit", integerProperty("Maximum result count, defaults to 500.")},
      }),
    },
    {
      "face_select",
      "Select one or more brush faces.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"faces", arrayProperty("Array of {objectId, faceIndex} face references.")},
        {"objectId", stringProperty("Optional single brush object id.")},
        {"objectIds",
         arrayProperty(
           "Optional object ids whose child brush faces should be selected.")},
        {"operationId", stringProperty("Optional MCP operation id target.")},
        {"operationIds", arrayProperty("Optional MCP operation id targets.")},
        {"faceIndex", integerProperty("Optional single face index.")},
        {"faceSemantic", stringProperty("Optional semantic face filter.")},
        {"normal", vec3Property("Optional normal vector face filter.")},
        {"normalTolerance", numberProperty("Normal matching tolerance.")},
      }),
    },
    {
      "face_texture_set",
      "Set face material and basic UV attributes for selected or specified faces.",
      McpMode::Edit,
      true,
      true,
      objectSchema(mergeProperties(
        QJsonObject{
          {"material", stringProperty("Optional material name.")},
          {"xOffset", numberProperty("Optional X texture offset.")},
          {"yOffset", numberProperty("Optional Y texture offset.")},
          {"xScale", numberProperty("Optional X texture scale.")},
          {"yScale", numberProperty("Optional Y texture scale.")},
          {"rotation", numberProperty("Optional texture rotation.")},
        },
        mergeProperties(faceTargetSchemaProperties(), semanticFaceSchemaProperties()))),
    },
    {
      "objects_delete",
      "Delete one or more entity, brush, patch, group, or layer nodes.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to delete.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for deleted object ids. Defaults to count.")},
        },
        {"objectIds"}),
    },
    {
      "objects_delete_by_filter",
      "Expert destructive helper that deletes selectable objects matched by a safe "
      "selection_filter-style query. Defaults exclude node:world, require selectable "
      "matches, and remove redundant descendants before deleting. Prefer structured "
      "selector_preview followed by objects_delete_by_selector for generated MCP "
      "content.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"type",
         stringProperty(
           "Optional node type: layer, group, entity, brush, or patch. Strongly "
           "recommended for destructive calls.")},
        {"classname", stringProperty("Optional entity classname.")},
        {"targetname", stringProperty("Optional entity targetname.")},
        {"material", stringProperty("Optional brush material name.")},
        {"query", stringProperty("Optional text query.")},
        {"min", vec3Property("Optional bounds minimum corner.")},
        {"max", vec3Property("Optional bounds maximum corner.")},
        {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
        {"leafOnly", boolProperty("Return only leaf matches before deletion.")},
        {"limit", integerProperty("Maximum matched object count, defaults to 100.")},
        {"idsMode",
         stringProperty(
           "none, count, sample, or full for deleted object ids. Defaults to count.")},
      }),
    },
    {
      "objects_delete_by_operation",
      "Compatibility helper that deletes the live selectable objects changed by a "
      "previous MCP operation. Prefer structured selector/module deletion for current "
      "workflows; use this only when an operation id is the clearest target.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for deleted object ids. Defaults to count.")},
        },
        {"operationId"}),
    },
    {
      "objects_transform",
      "Transform one or more selectable objects using translate, rotate, or scale. "
      "Prefer selector targets for module/route/part iteration so generated scene "
      "pieces can be stretched, moved, or rotated without deleting and rebuilding them. "
      "If objectIds, operationIds, and selector are omitted, transforms the current "
      "user selection.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to transform.")},
          {"operationId", liveOperationIdTargetProperty()},
          {"operationIds", liveOperationIdsTargetProperty()},
          {"selector",
           withDescription(
             selectorSchema(),
             "Optional structured selector for live transform targets. Used when "
             "objectIds and operationIds are omitted.")},
          {"operation",
           stringProperty("Transform operation: translate, rotate, or scale.")},
          {"delta", vec3Property("Translation delta in map units.")},
          {"axis", stringOrVec3Property("Rotation axis: x, y, z, or a [x,y,z] vector.")},
          {"angle", numberProperty("Rotation angle in degrees.")},
          {"scale", numberOrVec3Property("Scale factor number or [x,y,z] factors.")},
          {"center",
           vec3Property("Optional transform center. Defaults to object bounds center.")},
          {"idsMode",
           stringProperty(
             "Returned target id detail: count (default), sample, full, or none.")},
          {"sampleLimit",
           integerProperty("Maximum sampled ids when idsMode=sample. Defaults to 12.")},
        },
        {"operation"}),
    },
    {
      "group_create_from_selection",
      "Create a native TrenchBroom group from the current user selection. Groups are "
      "for visible Outliner organization and user selection convenience; keep semantic "
      "tracking in module/metadata/selector records.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"name", stringProperty("Native TrenchBroom group name.")},
          {"selectGroup",
           boolProperty("Select the new group after creation. Defaults to true.")},
          {"idsMode",
           stringProperty(
             "Returned group id detail: count (default), sample, full, or none.")},
          {"sampleLimit",
           integerProperty("Maximum sampled ids when idsMode=sample. Defaults to 12.")},
        },
        {"name"}),
    },
    {
      "group_inspect",
      "Inspect native TrenchBroom group structure by objectId/objectIds or by current "
      "selected group. Returns group name, bounds, child counts, edit state, and linked "
      "group summary. This does not read or write semantic metadata.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectId", stringProperty("Optional MCP group object id to inspect.")},
        {"objectIds", arrayProperty("Optional MCP group object ids to inspect.")},
        {"includeChildren",
         boolProperty(
           "Include direct child ids according to idsMode. Defaults to false.")},
        {"idsMode",
         stringProperty(
           "Returned child id detail when includeChildren=true: sample (default), "
           "count, full, or none.")},
        {"sampleLimit",
         integerProperty("Maximum sampled ids when idsMode=sample. Defaults to 12.")},
      }),
    },
    {
      "group_rename_selected",
      "Rename the currently selected native TrenchBroom group or groups. The current "
      "selection must contain only groups.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"name", stringProperty("New native TrenchBroom group name.")},
          {"idsMode",
           stringProperty(
             "Returned group id detail: count (default), sample, full, or none.")},
        },
        {"name"}),
    },
    {
      "group_ungroup_selected",
      "Ungroup the currently selected native TrenchBroom groups and leave their former "
      "children selected. Use for organization cleanup, not semantic metadata changes.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"idsMode",
         stringProperty(
           "Returned selected child id detail: count (default), sample, full, or none.")},
        {"sampleLimit",
         integerProperty("Maximum sampled ids when idsMode=sample. Defaults to 12.")},
      }),
    },
    {
      "map_validate",
      "Validate the active map and return compact passed/count/recoveryAction "
      "problem counts by default.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
        {"includeProblems",
         boolProperty(
           "Include a limited problems array. Defaults to false to keep validation "
           "responses compact.")},
        {"groupByType",
         boolProperty(
           "Return grouped problem counts with sample object ids and bounds. Useful for "
           "large generated maps with repeated warnings.")},
        {"limit", integerProperty("Maximum problem entries when includeProblems=true.")},
      }),
    },
    {
      "problems_check",
      "Return map validation problems with passed/recoveryAction and safe quick fix "
      "metadata. Each problem includes a line-sensitive id for problems_fix and a "
      "stableKey for before/after comparison.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
        {"limit", integerProperty("Maximum result count, defaults to 500.")},
      }),
    },
    {
      "problems_fix",
      "Apply one safe quick fix to selected validation problem ids.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"problemIds", arrayProperty("Problem ids from problems_check.")},
          {"quickFix", stringProperty("Safe quick fix description to apply.")},
          {"idsMode",
           stringProperty(
             "none, count, sample, or full for changed object ids. Defaults to count.")},
        },
        {"problemIds", "quickFix"}),
    },
    {
      "map_fix_all_safe",
      "Apply all currently available safe validation quick fixes.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
        {"idsMode",
         stringProperty(
           "none, count, sample, or full for changed object ids. Defaults to count.")},
      }),
    },
    {
      "compile_profiles_list",
      "List compilation profiles for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "compile_run",
      "Run a compilation profile through TrenchBroom's existing compile dialog.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"profile",
         stringProperty("Compilation profile name. Defaults to the first profile.")},
      }),
    },
    {
      "compile_log_tail",
      "Return recent text from the current compile dialog output.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"maxLines", integerProperty("Maximum lines to return, defaults to 80.")},
      }),
    },
    {
      "leaks_load_pointfile",
      "Load a compiler pointfile (.pts/.lin) into the active document.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute path to the pointfile.")},
        },
        {"path"}),
    },
    {
      "blockout_create_batch",
      "Create multiple blockout operations in one transaction. Prefer this over many "
      "atomic brush_create calls for architectural generation.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"name", stringProperty("Transaction label, defaults to MCP: Blockout batch.")},
          {"qualityPolicy", qualityPolicySchema()},
          {"material", stringProperty("Default brush material for the batch.")},
          {"requireMaterialAvailable",
           boolProperty(
             "When true, reject before mutation if any requested material is not "
             "currently loaded. Defaults to false.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated brushes.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode", changedIdsModeProperty()},
          {"defaultMetadata", genericMetadataSchema()},
          {"operations",
           arrayProperty(
             "Array of blockout operations. Primitive types include box, cylinder, "
             "prism, polyhedron, cylinder_sector, path_ribbon, repeat_translate, "
             "repeat_grid, stepped_mass, support_posts_between, arc_ramp, "
             "helical_ramp, ramp_between, and wedge. Room, corridor, sky shell, "
             "doorway, cover, and straight stair composition should use skill "
             "recipes/IR or explicit primitive operations. curved_corridor and legacy "
             "ramp remain for compatibility only. Each item must be an object with a "
             "type field; use "
             "tb_tools_search(detail=schema, query=\"blockout_create_batch operations\") "
             "for examples. Diagonal ramp_between can be valid but may return "
             "offAxisRampMayProduceNonGridVertices when side vertices are not "
             "grid-aligned.",
             blockoutBatchOperationSchema())},
        },
        {"operations"}),
    },
    {
      "python_generate_blockout",
      "Legacy script bridge that runs local Python source and compiles returned "
      "Blockout IR through blockout_create_batch. Prefer skill recipe scripts that "
      "write IR files, then use ir_compile_preview_from_file and ir_apply_from_file.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"script",
           stringProperty(
             "Python source. It must print a JSON object with an operations array to "
             "stdout.")},
          {"name",
           stringProperty("Transaction label, defaults to MCP: Python blockout.")},
          {"timeoutMs",
           integerProperty("Subprocess timeout in milliseconds. Defaults to 5000.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated brushes.")},
          {"detail", summaryIdsFullDetailProperty()},
          {"idsMode", changedIdsModeProperty()},
          {"material", stringProperty("Default material passed to blockout batch.")},
        },
        {"script"}),
    },
    {
      "heightmap_import_grayscale",
      "Import a local grayscale image as brush terrain. Defaults to terraced_brushes; "
      "adaptive_surface approximates a smoother heightfield with finer brush cells in "
      "more complex areas.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"imagePath", stringProperty("Local image path to read as a heightmap.")},
          {"origin", vec3Property("Terrain minimum origin, defaults to [0,0,0].")},
          {"cellSize", numberProperty("Map units per sampled pixel, defaults to 64.")},
          {"heightScale", numberProperty("Maximum generated height, defaults to 128.")},
          {"heightSteps", integerProperty("Quantization steps, defaults to 8.")},
          {"maxSize",
           integerProperty(
             "Maximum sampled image dimension before downsampling, defaults to 64.")},
          {"maxBrushes",
           integerProperty("Maximum merged brushes to create, defaults to 512.")},
          {"mode",
           stringProperty(
             "Import mode: terraced_brushes or adaptive_surface. Defaults to "
             "terraced_brushes.")},
          {"minCellSize",
           numberProperty(
             "adaptive_surface minimum cell size in map units. Defaults to cellSize.")},
          {"maxCellSize",
           numberProperty(
             "adaptive_surface maximum cell size in map units. Defaults to 4 * "
             "cellSize.")},
          {"errorTolerance",
           numberProperty(
             "adaptive_surface height error tolerance in map units before a cell "
             "subdivides. Lower values create more brushes.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated terrain brushes.")},
          {"idsMode",
           stringProperty(
             "Object id verbosity: none, count, sample, or full. Maps to summary/ids "
             "detail internally.")},
          {"detail", summaryIdsFullDetailProperty()},
        },
        {"imagePath"}),
    },
    {
      "heightmap_preview_grayscale",
      "Preview a local grayscale heightmap import without committing brushes.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"imagePath", stringProperty("Local image path to read as a heightmap.")},
          {"origin", vec3Property("Terrain minimum origin, defaults to [0,0,0].")},
          {"cellSize", numberProperty("Map units per sampled pixel, defaults to 64.")},
          {"heightScale", numberProperty("Maximum generated height, defaults to 128.")},
          {"heightSteps", integerProperty("Quantization steps, defaults to 8.")},
          {"maxSize",
           integerProperty(
             "Maximum sampled image dimension before downsampling, defaults to 64.")},
          {"maxBrushes",
           integerProperty("Maximum merged brushes to allow, defaults to 512.")},
          {"mode",
           stringProperty(
             "Preview mode: terraced_brushes or adaptive_surface. Defaults to "
             "terraced_brushes.")},
          {"minCellSize",
           numberProperty("adaptive_surface minimum cell size in map units.")},
          {"maxCellSize",
           numberProperty("adaptive_surface maximum cell size in map units.")},
          {"errorTolerance",
           numberProperty("adaptive_surface height error tolerance in map units.")},
          {"material", stringProperty("Brush material for previewed operations.")},
        },
        {"imagePath"}),
    },
    {
      "ir_validate",
      "Validate minimal TrenchBroom MCP scene IR without mutating the map. IR is a "
      "small JSON wrapper around existing atomic operations: operations for "
      "blockout_create_batch and entities for entity_create_checked_batch.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"ir",
         QJsonObject{
           {"type", "object"},
           {"description",
            "Optional IR object. It must contain operations and/or entities arrays; "
            "operations entries require type and entity entries require classname."},
           {"additionalProperties", true},
         }},
        {"schemaVersion",
         integerProperty("Optional flattened IR schema version. Current version is 1.")},
        {"qualityPolicy", qualityPolicySchema()},
        {"requireMaterialAvailable",
         boolProperty(
           "When true, reject missing requested materials before mutation. Defaults "
           "to false.")},
        {"applyMode",
         stringProperty(
           "create or replace_module. Replacement preview reports exact module "
           "revision, content hash, and canonical object guards.")},
        {"moduleId", stringProperty("Module id used by replace_module preview.")},
        {"defaultMetadata", genericMetadataSchema()},
        {"operations",
         arrayProperty(
           "Optional blockout_create_batch operations array. Every item must be an "
           "object with a type.")},
        {"entities",
         arrayProperty(
           "Optional entity_create_checked_batch entities array. Every item must be "
           "an object with classname.")},
      }),
    },
    {
      "ir_compile_preview",
      "Preview minimal scene IR compile counts, parts, moduleId, and warnings without "
      "committing a transaction.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"ir",
         QJsonObject{
           {"type", "object"},
           {"description",
            "Optional IR object. It must contain operations and/or entities arrays; "
            "operations entries require type and entity entries require classname."},
           {"additionalProperties", true},
         }},
        {"schemaVersion",
         integerProperty("Optional flattened IR schema version. Current version is 1.")},
        {"qualityPolicy", qualityPolicySchema()},
        {"requireMaterialAvailable",
         boolProperty(
           "When true, reject missing requested materials before mutation. Defaults "
           "to false.")},
        {"applyMode",
         stringProperty(
           "create or replace_module. Replacement preview reports exact module "
           "revision, content hash, and canonical object guards.")},
        {"moduleId", stringProperty("Module id used by replace_module preview.")},
        {"defaultMetadata", genericMetadataSchema()},
        {"operations",
         arrayProperty(
           "Optional blockout_create_batch operations array. Every item must be an "
           "object with a type.")},
        {"entities",
         arrayProperty(
           "Optional entity_create_checked_batch entities array. Every item must be "
           "an object with classname.")},
      }),
    },
    {
      "ir_compile_preview_from_file",
      "Read a local JSON IR file and preview the same minimal scene IR without "
      "mutating the map. Use this when generated IR would be too large for the "
      "conversation context. Successful valid previews return a session previewId "
      "that ir_apply_from_file can reuse without resending the path.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute local path to a JSON IR file.")},
          {"qualityPolicy",
           withDescription(
             qualityPolicySchema(),
             "Optional request override for the IR qualityPolicy.")},
          {"requireMaterialAvailable",
           boolProperty(
             "Optional strict material availability override. Defaults to false.")},
          {"applyMode",
           stringProperty(
             "Optional create or replace_module override. File replacement must be "
             "applied later with the returned previewId.")},
        },
        {"path"}),
    },
    {
      "ir_apply",
      "Apply minimal scene IR by dispatching to existing atomic MCP operations in "
      "transactions. This is an orchestration convenience for skill-generated IR, not "
      "a scene prefab.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"ir",
         QJsonObject{
           {"type", "object"},
           {"description",
            "Optional IR object. It must contain operations and/or entities arrays; "
            "operations entries require type and entity entries require classname."},
           {"additionalProperties", true},
         }},
        {"schemaVersion",
         integerProperty("Optional flattened IR schema version. Current version is 1.")},
        {"qualityPolicy", qualityPolicySchema()},
        {"requireMaterialAvailable",
         boolProperty(
           "When true, reject missing requested materials before mutation. Defaults "
           "to false.")},
        {"applyMode",
         stringProperty(
           "create or replace_module. replace_module requires the exact expected "
           "guards returned by ir_compile_preview.")},
        {"operations",
         arrayProperty(
           "Optional blockout_create_batch operations array. Every item must be an "
           "object with a type.")},
        {"entities",
         arrayProperty(
           "Optional entity_create_checked_batch entities array. Every item must be "
           "an object with classname.")},
        {"moduleId", stringProperty("Optional module id copied into defaultMetadata.")},
        {"expectedIrHash",
         stringProperty("Required preview IR hash for replace_module.")},
        {"expectedTargetModuleRevision",
         integerProperty("Required preview module revision for replace_module.")},
        {"expectedTargetModuleContentHash",
         stringProperty("Required preview module content hash for replace_module.")},
        {"expectedTargetCanonicalObjectIds",
         arrayProperty(
           "Required exact canonical live object set returned by replacement preview.")},
        {"defaultMetadata", genericMetadataSchema()},
        {"name", stringProperty("Optional blockout transaction name.")},
        {"grid", numberProperty("Optional grid snap size.")},
        {"material", stringProperty("Optional default material.")},
        {"select", boolProperty("Select generated blockout geometry.")},
        {"idsMode",
         stringProperty(
           "Changed object id verbosity: none, count, sample, or full. Defaults to "
           "count.")},
      }),
    },
    {
      "ir_apply_from_file",
      "Read a local JSON IR file and apply it through existing atomic MCP operations. "
      "The file is transport only; it does not add scene prefab semantics. Pass either "
      "path or a previewId returned by ir_compile_preview_from_file.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"path", stringProperty("Absolute local path to a JSON IR file.")},
        {"previewId",
         stringProperty(
           "Optional cached preview id from ir_compile_preview_from_file. When "
           "provided, path is read from the cache and the file hash/document "
           "fingerprint must still match.")},
        {"qualityPolicy",
         withDescription(
           qualityPolicySchema(), "Optional request override for the IR qualityPolicy.")},
        {"requireMaterialAvailable",
         boolProperty(
           "Optional strict material availability override. Defaults to false.")},
        {"applyMode",
         stringProperty(
           "Optional create or replace_module override. replace_module requires "
           "previewId and does not accept a path-only apply.")},
        {"expectedDocumentPath",
         stringProperty("Optional active document guard before mutating.")},
        {"idsMode",
         stringProperty(
           "Changed object id verbosity: none, count, sample, or full. Defaults to "
           "count.")},
      }),
    },
    {
      "blockout_create_curved_corridor",
      "Create a curved corridor as one transaction using floor, ceiling, inner wall, "
      "outer wall, and optional caps.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"center", vec3Property("Corridor center; X/Y define arc origin.")},
        {"innerRadius", numberProperty("Playable inner radius.")},
        {"outerRadius", numberProperty("Playable outer radius.")},
        {"startAngle", numberProperty("Start angle in degrees.")},
        {"turnDegrees",
         numberProperty(
           "Arc length in degrees. Must be finite, non-zero, and at most 360.")},
        {"height", numberProperty("Playable height, defaults to 128.")},
        {"slopeStartZ",
         numberProperty(
           "Starting Z offset for a terraced uphill/downhill arc. This does not create "
           "continuous sloped top faces.")},
        {"slopeEndZ",
         numberProperty(
           "Ending Z offset for a terraced uphill/downhill arc. Use arc_ramp or "
           "helical_ramp for true sloped curved surfaces.")},
        {"segments", integerProperty("Segment count, defaults to 12.")},
        {"wallThickness",
         numberProperty(
           "Thickness for inner_wall and outer_wall parts, defaults to 16.")},
        {"floorThickness",
         numberProperty("Thickness for floor part slabs, defaults to 16.")},
        {"ceilingThickness",
         numberProperty("Thickness for ceiling part slabs, defaults to 16.")},
        {"caps", stringProperty("none, start, end, or both. Defaults to none.")},
        {"snapMode",
         stringProperty(
           "XY vertex snap mode: radial, grid, or none. Defaults to radial to keep "
           "arc boundaries visually continuous.")},
        {"qualityPolicy", qualityPolicySchema()},
        {"grid", numberProperty("Grid size for Z/thickness snapping and grid mode.")},
        {"material", stringProperty("Brush material, defaults to current material.")},
        {"defaultMetadata", genericMetadataSchema()},
        {"metadata", genericMetadataSchema()},
        {"parts",
         arrayProperty(
           "Optional parts to generate: floor, ceiling, inner_wall, outer_wall, "
           "start_cap, end_cap.")},
        {"partMaterials", genericMetadataSchema()},
        {"partMetadata", genericMetadataSchema()},
        {"select", boolProperty("Select generated brushes.")},
        {"detail", summaryIdsFullDetailProperty()},
      }),
    },
    {
      "blockout_validate",
      "Validate Blockout IR dimensions without modifying the map.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"type",
           stringProperty(
             "IR type for legacy validation. Supported values: ramp or spiral_stairs. "
             "Room, corridor, sky shell, doorway, cover, and straight stair composition "
             "moved to trenchbroom-mcp-scene-workflow recipes.")},
          {"min", vec3Property("Minimum corner.")},
          {"max", vec3Property("Maximum corner.")},
          {"thickness", numberProperty("Optional ramp thickness.")},
          {"innerRadius", numberProperty("Optional spiral stair inner radius.")},
          {"outerRadius", numberProperty("Optional spiral stair outer radius.")},
          {"stepHeight", numberProperty("Optional spiral stair step height.")},
          {"turnDegrees", numberProperty("Optional spiral stair turn angle.")},
        },
        {"type", "min", "max"}),
    },
    {
      "blockout_create_spiral_stairs",
      "Create deterministic solid spiral stairs with matching center column and landing.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"center", vec3Property("Stair center; X/Y define the rotation origin.")},
        {"innerRadius", numberProperty("Inner radius, defaults to 32.")},
        {"outerRadius", numberProperty("Outer radius, defaults to 128.")},
        {"steps", integerProperty("Step count, defaults to 24.")},
        {"stepHeight", numberProperty("Step height, defaults to 8.")},
        {"startAngle", numberProperty("Start angle in degrees, defaults to 0.")},
        {"turnDegrees",
         numberProperty(
           "Total turn angle, defaults to 360. Must be finite, non-zero, and at "
           "most 360.")},
        {"clockwise", boolProperty("Rotate clockwise instead of counter-clockwise.")},
        {"baseZ", numberProperty("Base Z, defaults to center.z.")},
        {"column", boolProperty("Create a center column, defaults to true.")},
        {"landing", boolProperty("Create an exit landing, defaults to true.")},
        {"material", stringProperty("Brush material, defaults to current material.")},
        {"select", boolProperty("Select generated brushes.")},
        {"idsMode",
         stringProperty(
           "none, count, sample, or full for changed object ids. Defaults to count; "
           "full maps to detail=ids.")},
        {"detail", summaryIdsFullDetailProperty()},
      }),
    },
    {
      "geometry_analyze_selection",
      "Analyze selected brush geometry for vertices, bounds, convexity, grid alignment, "
      "and materials.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"grid", numberProperty("Grid size for alignment checks, defaults to 1.")},
        {"detail", summaryIdsFullDetailProperty()},
        {"maxBrushes",
         integerProperty(
           "Maximum per-brush entries returned for ids/full detail, defaults to 100.")},
        {"includeVertices",
         boolProperty("Include vertex arrays for detail=full, defaults to false.")},
      }),
    },
    {
      "geometry_csg_selection",
      "Run native TrenchBroom CSG on the current selection. This is a "
      "selection-only boolean geometry primitive; prepare complex targets first with "
      "selection_set, objects_select_by_selector, or manual user selection.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"operation",
           stringProperty(
             "CSG operation: convex_merge, subtract, intersect, or hollow. "
             "convex_merge accepts selected brushes or brush faces; the others require "
             "selected brush nodes only.")},
          {"expectedDocumentPath",
           stringProperty(
             "Optional active document guard. Fails before mutation if the active map "
             "path differs.")},
          {"idsMode",
           stringProperty(
             "count, sample, or full for changed/deleted object ids. Defaults to "
             "sample; full returns complete changedObjectIds and deletedObjectIds.")},
        },
        {"operation"}),
    },
    {
      "geometry_analyze_slopes",
      "Analyze live brush slope faces for ramp/wedge/surf/slide validation. Reports "
      "face normals, slope angle, rise direction, and whether each slope ascends or "
      "descends along routeDirection or start -> end. This is geometry/mapper "
      "semantics only, not gameplay difficulty analysis. Summary includes passed, "
      "slopeCount, warning samples, and recoveryAction. If operationIds, objectIds, "
      "and selector are omitted, analyzes the current user-selected brushes.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationId", liveOperationIdTargetProperty("brush objects")},
        {"operationIds", liveOperationIdsTargetProperty("brush objects")},
        {"objectIds", arrayProperty("Explicit MCP object ids to analyze.")},
        {"selector",
         withDescription(
           selectorSchema(),
           "Structured selector for live route/ramp targets. Prefer this over "
           "fetching full objectIds when filtering by moduleId/part/role/routeId.")},
        {"routeDirection",
         vec3Property(
           "Optional travel direction vector. X/Y determine whether slopes are "
           "ascending, descending, or cross_slope.")},
        {"start",
         vec3Property(
           "Optional route start. When start and end are provided, start -> end is "
           "used as routeDirection.")},
        {"end",
         vec3Property(
           "Optional route end. When start and end are provided, start -> end is "
           "used as routeDirection.")},
        {"minSlopeDegrees",
         numberProperty(
           "Minimum non-flat slope angle to report. Defaults to 0.5 degrees.")},
        {"maxSlopeDegrees",
         numberProperty("Maximum slope angle to report. Defaults to 89 degrees.")},
        {"detail",
         stringProperty(
           "summary or full. Defaults to summary. Summary returns counts and a small "
           "slopeSample; full also returns every slope face.")},
      }),
    },
    {
      "geometry_analyze_route_continuity",
      "Analyze ordered route brush surfaces for playable continuity. Reports each "
      "target's upward playable face and each adjacent seam's verticalStep, "
      "horizontalGap, fullWidthContinuous, positiveGap, overlapDepth, and "
      "classification. edgeGapMax is retained as a legacy endpoint-distance metric "
      "and does not necessarily represent an empty seam. New workflows should use "
      "positiveGap, overlapDepth, and acceptancePassed. Summary includes passed, max "
      "gap/step fields, failing seam samples, and recoveryAction. Same-height overlaps "
      "are reported as overlap_continuous_height and remain continuous. If "
      "operationIds, objectIds, and selector are omitted, analyzes the current "
      "user-selected brushes.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationId", liveOperationIdTargetProperty("brush objects")},
        {"operationIds", liveOperationIdsTargetProperty("brush objects")},
        {"objectIds",
         arrayProperty(
           "Explicit MCP object ids to analyze. The analyzer sorts them along the "
           "route direction before comparing adjacent surfaces.")},
        {"selector",
         withDescription(
           selectorSchema(),
           "Structured selector for live route targets. Prefer this over operationIds "
           "when an operation also contains walls, rails, supports, markers, or caps. "
           "Common filters are moduleId plus role:\"walkable\" or part:\"floor\".")},
        {"routeDirection",
         vec3Property(
           "Optional travel direction vector. X/Y determine route ordering and seam "
           "entry/exit heights.")},
        {"start",
         vec3Property(
           "Optional route start. When start and end are provided, start -> end is "
           "used as routeDirection.")},
        {"end",
         vec3Property(
           "Optional route end. When start and end are provided, start -> end is "
           "used as routeDirection.")},
        {"verticalTolerance",
         numberProperty(
           "Maximum absolute seam height difference treated as continuous. Defaults "
           "to 0.5 map units.")},
        {"horizontalTolerance",
         numberProperty(
           "Maximum positive route-direction gap treated as continuous. Defaults to "
           "1 map unit.")},
        {"continuityMode",
         stringProperty(
           "Legacy route validation mode: continuous (strict default), stepped, "
           "jump_gaps. Prefer routeMode for new workflows.")},
        {"routeMode",
         stringProperty(
           "continuous, stepped, jump_chain, spiral, or closed_loop. Also accepts "
           "validation profile aliases walkable_continuous, spiral_ascending, "
           "jump_chain, stairs_or_steps, and slide_or_surf.")},
        {"validationMode",
         stringProperty(
           "Alias for routeMode used by recipes/skills. continuityMode remains "
           "accepted for compatibility.")},
        {"maxStepHeight",
         numberProperty(
           "Used with continuityMode=stepped. Maximum absolute step height treated as "
           "semantically continuous. Defaults to 24.")},
        {"maxJumpGap",
         numberProperty(
           "Used with continuityMode=jump_gaps. Maximum horizontal gap treated as an "
           "intentional jump gap. Defaults to 128.")},
        {"minUpNormal",
         numberProperty(
           "Minimum face normal.z for playable surfaces. Defaults to 0.2 so ramps and "
           "flat tops are included while walls are ignored.")},
        {"orderBy",
         stringProperty(
           "Route seam order: projection (default) or metadataOrder for ordered route "
           "metadata.")},
        {"closedLoop",
         boolProperty(
           "When true, also checks the final ordered surface back to the first and "
           "marks that seam with loopClosure.")},
        {"qualityPolicy", qualityPolicySchema()},
        {"detail",
         stringProperty(
           "summary or full. Defaults to summary. Summary returns continuity totals "
           "and small seam/surface samples; full also returns every surface, seam, and "
           "unsupported object id.")},
      }),
    },
    {
      "geometry_analyze_shell_seams",
      "Analyze annotated shell seam metadata on generated brush shells. This "
      "read-only validator groups brush metadata shellSeams by key and checks "
      "participant counts, shared vertex coordinates, and maxVertexDelta. It does "
      "not infer intended seams from arbitrary old map geometry; targets without "
      "shellSeams should be rebuilt with an annotated recipe or reviewed visually. "
      "If operationIds, objectIds, and selector are omitted, analyzes the current "
      "user-selected brushes.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationId", liveOperationIdTargetProperty("brush objects")},
        {"operationIds", liveOperationIdsTargetProperty("brush objects")},
        {"objectIds", arrayProperty("Explicit MCP brush object ids to analyze.")},
        {"selector",
         withDescription(
           selectorSchema(),
           "Structured selector for generated shell targets. For recipe output, "
           "prefer moduleId or moduleId plus routeId.")},
        {"tolerance",
         numberProperty(
           "Maximum per-vertex offset in map units accepted for matching seam "
           "annotations. Defaults to 0.01.")},
        {"detail",
         stringProperty(
           "summary or full. Defaults to summary. Summary returns counts and seam "
           "samples; full also returns every seam diagnostic.")},
      }),
    },
    {
      "brush_metadata_set",
      "Legacy object-id metadata setter. Prefer passing defaultMetadata/metadata to "
      "creation tools or using structured selectors/modules for recovery.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Brush object ids to annotate.")},
          {"metadata", routeMetadataSchema()},
        },
        {"objectIds", "metadata"}),
    },
    {
      "brush_metadata_get",
      "Legacy object-id metadata reader. Prefer selector_preview or module_inspect for "
      "structured metadata recovery.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Brush object ids to inspect.")},
        },
        {"objectIds"}),
    },
    {
      "selection_by_metadata",
      "Legacy metadata selector. Prefer structured selectors with selector_preview, "
      "objects_select_by_selector, objects_delete_by_selector, and "
      "render_review_selector.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"routeId", stringProperty("Optional route id to match exactly.")},
        {"intent", stringProperty("Optional intent to match exactly.")},
        {"difficulty", stringProperty("Optional difficulty to match exactly.")},
        {"movementType", stringProperty("Optional movement type to match exactly.")},
        {"order", numberProperty("Optional exact route order to match.")},
        {"metadata", routeMetadataSchema()},
        {"select", boolProperty("Replace selection with matched live brush nodes.")},
        {"limit", integerProperty("Maximum result count, defaults to 100.")},
      }),
    },
    {
      "kz_distance_analyze_chain",
      "Compatibility alias for route_geometry_analyze_chain. Returns geometric route "
      "facts only. Prefer geometry_analyze_route_continuity for route validation; "
      "difficulty should be judged by the Agent using project/domain context.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional ordered brush object ids to analyze.")},
        {"routeId",
         stringProperty("Optional metadata routeId. Used when objectIds is omitted.")},
        {"orderBy",
         stringProperty(
           "When using routeId, use metadataOrder to sort by metadata order. Defaults "
           "to metadataOrder.")},
        {"movementType", stringProperty("Optional movement type such as bhop or LJ.")},
        {"playerHull",
         vec3Property(
           "Optional player hull size [width, depth, height], defaults to [32,32,72].")},
      }),
    },
    {
      "route_geometry_analyze_chain",
      "Analyze ordered brush-platform geometry facts such as edge gap, effective "
      "distance, height delta, lateral offset, and landing window area. This tool "
      "does not classify gameplay difficulty or pass/fail viability. Prefer "
      "geometry_analyze_route_continuity for current route validation workflows.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional ordered brush object ids to analyze.")},
        {"routeId",
         stringProperty("Optional metadata routeId. Used when objectIds is omitted.")},
        {"orderBy",
         stringProperty(
           "When using routeId, use metadataOrder to sort by metadata order. Defaults "
           "to metadataOrder.")},
        {"movementType",
         stringProperty("Optional movement label used only for result context.")},
        {"playerHull",
         vec3Property(
           "Optional player hull size [width, depth, height], defaults to [32,32,72].")},
      }),
    },
    {
      "blockout_validate_spiral_stairs",
      "Validate selected spiral-stair brushes generated through MCP.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationId", stringProperty("Optional MCP operation id to validate.")},
        {"center", vec3Property("Optional expected stair center.")},
        {"innerRadius", numberProperty("Optional expected inner radius.")},
        {"outerRadius", numberProperty("Optional expected outer radius.")},
        {"steps", integerProperty("Optional expected step count.")},
        {"stepHeight", numberProperty("Optional expected step height.")},
        {"turnDegrees", numberProperty("Optional expected turn angle.")},
        {"grid", numberProperty("Grid size for alignment checks, defaults to 1.")},
      }),
    },
    {
      "tb_tools_search",
      "Search available TrenchBroom MCP tools by name, category, or description. Use "
      "detail=schema when exact parameters are needed.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query.")},
        {"category", stringProperty("Optional category such as blockout or brush.")},
        {"detail", stringProperty("summary or schema. Defaults to summary.")},
      }),
    },
  };

  static const auto CatalogWithMetadata = [&] {
    auto tools = Catalog;
    for (auto& tool : tools)
    {
      if (tool.name.startsWith("brush_create"))
      {
        tool.category = "brush";
        tool.expert = true;
        if (
          tool.name == "brush_create_boxes_batch"
          || tool.name == "brush_create_polygon_batch")
        {
          tool.expert = false;
        }
        if (
          tool.name != "brush_create_boxes_batch"
          && tool.name != "brush_create_polygon_batch")
        {
          auto schema = tool.inputSchema;
          auto properties = schema.value("properties").toObject();
          properties.insert(
            "requireMaterialAvailable",
            boolProperty(
              "When true, reject before mutation if the requested material is not "
              "currently loaded. Defaults to false."));
          schema.insert("properties", properties);
          tool.inputSchema = schema;
        }
      }
      else if (tool.name.startsWith("blockout_"))
      {
        tool.category = "blockout";
        if (
          tool.name == "blockout_create_curved_corridor"
          || tool.name == "blockout_create_spiral_stairs")
        {
          auto schema = tool.inputSchema;
          auto properties = schema.value("properties").toObject();
          properties.insert(
            "requireMaterialAvailable",
            boolProperty(
              "When true, reject before mutation if any requested material is not "
              "currently loaded. Defaults to false."));
          schema.insert("properties", properties);
          tool.inputSchema = schema;
        }
      }
      else if (tool.name.startsWith("operation_") || tool.name.startsWith("history_"))
      {
        tool.category = "operation";
      }
      else if (tool.name.startsWith("selection_"))
      {
        tool.category = "selection";
      }
      else if (tool.name.startsWith("viewport_"))
      {
        tool.category = "viewport";
        tool.lifecycle = "experimental";
      }
      else if (tool.name.startsWith("asset_"))
      {
        tool.category = "asset";
      }
      else if (tool.name.startsWith("objects_"))
      {
        tool.category = "object";
      }
      else if (tool.name.startsWith("group_"))
      {
        tool.category = "group";
        if (tool.name == "group_rename_selected" || tool.name == "group_ungroup_selected")
        {
          tool.expert = true;
        }
      }
      else if (tool.name.startsWith("texture_") || tool.name.startsWith("face_"))
      {
        tool.category = "texture";
      }
      else if (tool.name.startsWith("entity_") || tool.name.startsWith("fgd_"))
      {
        tool.category = "entity";
      }
      else if (tool.name.startsWith("python_"))
      {
        tool.category = "python";
        tool.lifecycle = "legacy";
      }
      else if (tool.name.startsWith("heightmap_"))
      {
        tool.category = "heightmap";
      }
      else if (tool.name.startsWith("geometry_"))
      {
        tool.category = "geometry";
      }
      else if (
        tool.name == "shape_library_list" || tool.name.startsWith("brush_metadata_")
        || tool.name == "selection_by_metadata"
        || tool.name == "route_geometry_analyze_chain")
      {
        tool.category = "route";
        tool.lifecycle = "legacy";
      }
      else if (tool.name == "kz_distance_analyze_chain")
      {
        tool.category = "route";
        tool.expert = true;
        tool.lifecycle = "legacy";
      }
      else if (
        tool.name == "tb_status" || tool.name == "tb_doctor"
        || tool.name == "tb_tools_search")
      {
        tool.category = "core";
      }
      if (
        tool.name == "history_list" || tool.name == "operation_select"
        || tool.name == "objects_delete_by_filter"
        || tool.name == "objects_delete_by_operation"
        || tool.name == "python_generate_blockout"
        || tool.name.startsWith("brush_metadata_") || tool.name == "selection_by_metadata"
        || tool.name == "route_geometry_analyze_chain"
        || tool.name == "kz_distance_analyze_chain")
      {
        tool.lifecycle = "legacy";
      }
      if (!tool.implemented)
      {
        tool.lifecycle = "experimental";
      }
      tool.costClass = isLongRunningTool(tool) ? McpToolCostClass::Long
                       : isFastTool(tool)      ? McpToolCostClass::Fast
                                               : McpToolCostClass::Normal;
      assertKnownToolMetadata(tool);
      addExpectedDocumentPathGuardSchema(tool);
    }
    return tools;
  }();

  return CatalogWithMetadata;
}

QString toolProfileName(const McpToolProfile profile)
{
  switch (profile)
  {
  case McpToolProfile::Core:
    return "Core";
  case McpToolProfile::Modeling:
    return "Modeling";
  case McpToolProfile::Full:
    return "Full";
  }
  return "Modeling";
}

std::optional<McpToolProfile> parseToolProfile(const QString& profile)
{
  const auto normalized = profile.trimmed().toLower();
  if (normalized == "core")
  {
    return McpToolProfile::Core;
  }
  if (normalized == "modeling")
  {
    return McpToolProfile::Modeling;
  }
  if (normalized == "balanced")
  {
    return McpToolProfile::Modeling;
  }
  if (normalized == "full")
  {
    return McpToolProfile::Full;
  }
  return std::nullopt;
}

QString toolCostClassName(const McpToolCostClass costClass)
{
  switch (costClass)
  {
  case McpToolCostClass::Fast:
    return "Fast";
  case McpToolCostClass::Normal:
    return "Normal";
  case McpToolCostClass::Long:
    return "Long";
  }
  return "Normal";
}

int toolResponseTimeoutMs(const McpToolCostClass costClass)
{
  switch (costClass)
  {
  case McpToolCostClass::Fast:
    return 10'000;
  case McpToolCostClass::Normal:
    return 30'000;
  case McpToolCostClass::Long:
    return 120'000;
  }
  return 30'000;
}

McpToolCostClass toolCostClassForName(const QString& name)
{
  const auto tool = findToolDefinition(name);
  return tool ? tool->costClass : McpToolCostClass::Normal;
}

bool visibleInModelingProfile(const McpToolDefinition& tool)
{
  static constexpr auto ToolNames = std::array{
    "tb_status",
    "tb_doctor",
    "tb_tools_search",
    "documents_list",
    "documents_open_verified",
    "documents_save_current",
    "documents_save_as",
    "map_snapshot",
    "selection_inspect",
    "entity_link_chain_inspect",
    "render_review_current_scene",
    "render_review_operation",
    "selector_preview",
    "objects_select_by_selector",
    "objects_delete_by_selector",
    "render_review_selector",
    "module_list",
    "module_inspect",
    "module_render_review",
    "module_validate",
    "module_compact",
    "operation_inspect",
    "operation_validate",
    "history_status",
    "history_undo_mcp",
    "history_undo_to_operation",
    "history_redo_mcp",
    "brush_create_boxes_batch",
    "brush_create_polygon_batch",
    "blockout_create_batch",
    "heightmap_import_grayscale",
    "heightmap_preview_grayscale",
    "ir_compile_preview",
    "ir_compile_preview_from_file",
    "ir_apply",
    "ir_apply_from_file",
    "geometry_csg_selection",
    "geometry_analyze_selection",
    "geometry_analyze_slopes",
    "geometry_analyze_route_continuity",
    "geometry_analyze_shell_seams",
    "objects_transform",
    "group_create_from_selection",
    "group_inspect",
    "entity_create_checked",
    "entity_create_checked_batch",
    "entity_properties_update",
    "entity_properties_delete",
    "texture_search",
    "texture_apply_by_filter",
    "texture_align_face",
    "map_validate",
    "problems_check",
  };

  return std::ranges::any_of(
    ToolNames, [&](const auto* toolName) { return tool.name == toolName; });
}

bool visibleInProfile(const McpToolDefinition& tool, const McpToolProfile profile)
{
  if (profile == McpToolProfile::Full)
  {
    return true;
  }
  if (profile == McpToolProfile::Core)
  {
    return tool.name == "tb_status" || tool.name == "tb_doctor"
           || tool.name == "tb_tools_search" || tool.name == "blockout_create_batch"
           || tool.name.startsWith("operation_")
           || tool.name.startsWith("viewport_capture")
           || tool.name == "geometry_analyze_selection"
           || tool.name == "blockout_validate";
  }
  if (profile == McpToolProfile::Modeling)
  {
    return visibleInModelingProfile(tool);
  }
  return false;
}

std::optional<McpToolDefinition> findToolDefinition(const QString& name)
{
  const auto& catalog = defaultToolCatalog();
  const auto it =
    std::ranges::find_if(catalog, [&](const auto& tool) { return tool.name == name; });
  if (it == catalog.end())
  {
    return std::nullopt;
  }
  return *it;
}

bool canCallTool(const McpToolDefinition& tool, const McpMode mode)
{
  return tool.implemented && allowsMode(mode, tool.requiredMode);
}

namespace
{

bool isWeakToolSearchToken(const QString& token)
{
  static constexpr auto WeakTokens = std::array{
    "argument",
    "arguments",
    "format",
    "input",
    "inputs",
    "json",
    "mcp",
    "output",
    "outputs",
    "parameter",
    "parameters",
    "schema",
    "tool",
    "tools",
  };

  return std::ranges::any_of(
    WeakTokens, [&](const auto* weakToken) { return token == weakToken; });
}

QStringList toolSearchTokens(const QString& query)
{
  auto tokens = QStringList{};
  auto token = QString{};

  const auto flushToken = [&] {
    if (token.size() >= 2 && !isWeakToolSearchToken(token))
    {
      tokens.push_back(token);
    }
    token.clear();
  };

  for (const auto ch : query.trimmed().toLower())
  {
    if (ch.isLetterOrNumber())
    {
      token.push_back(ch);
    }
    else
    {
      flushToken();
    }
  }
  flushToken();

  tokens.removeDuplicates();
  return tokens;
}

QString searchableToolText(const McpToolDefinition& tool, const bool includeSchema)
{
  auto expandedName = tool.name;
  expandedName.replace("_", " ");

  auto text =
    QStringList{
      tool.name,
      expandedName,
      tool.description,
      tool.category,
      tool.lifecycle,
    }
      .join("\n")
      .toLower();

  if (includeSchema)
  {
    text += "\n";
    text +=
      QString::fromUtf8(QJsonDocument{tool.inputSchema}.toJson(QJsonDocument::Compact))
        .toLower();
  }

  return text;
}

bool toolMatchesSearch(
  const McpToolDefinition& tool, const QStringList& tokens, const bool includeSchema)
{
  if (tokens.isEmpty())
  {
    return true;
  }

  const auto text = searchableToolText(tool, includeSchema);
  return std::ranges::all_of(
    tokens, [&](const auto& token) { return text.contains(token); });
}

bool queryContainsExactToolName(const QString& normalizedQuery, const QString& toolName)
{
  const auto normalizedName = toolName.toLower();
  auto start = qsizetype{0};
  while ((start = normalizedQuery.indexOf(normalizedName, start)) >= 0)
  {
    const auto before =
      start == 0 ? QChar{} : normalizedQuery.at(static_cast<qsizetype>(start - 1));
    const auto afterIndex = start + normalizedName.size();
    const auto after =
      afterIndex >= normalizedQuery.size() ? QChar{} : normalizedQuery.at(afterIndex);
    const auto beforeBoundary =
      before.isNull() || (!before.isLetterOrNumber() && before != '_');
    const auto afterBoundary =
      after.isNull() || (!after.isLetterOrNumber() && after != '_');
    if (beforeBoundary && afterBoundary)
    {
      return true;
    }
    ++start;
  }
  return false;
}

bool queryHasAnyExactToolName(const QString& normalizedQuery)
{
  if (normalizedQuery.isEmpty())
  {
    return false;
  }
  return std::ranges::any_of(defaultToolCatalog(), [&](const auto& tool) {
    return queryContainsExactToolName(normalizedQuery, tool.name);
  });
}

} // namespace

QJsonArray toolsSearchJson(
  const QString& query,
  const QString& category,
  const QString& detail,
  const McpMode mode,
  const McpToolProfile profile)
{
  const auto normalizedQuery = query.trimmed().toLower();
  const auto normalizedCategory = category.trimmed().toLower();
  const auto includeSchema =
    detail.trimmed().toLower() == "schema" || detail.trimmed().toLower() == "full";
  const auto tokens = toolSearchTokens(normalizedQuery);

  const auto appendTool =
    [&](QJsonArray& result, const auto& tool, const bool includeToolSchema) {
      auto object = toMcpToolSummaryJson(tool, profile);
      if (includeToolSchema)
      {
        object.insert("inputSchema", tool.inputSchema);
      }
      else if (includeSchema)
      {
        object.insert("schemaAvailable", true);
        object.insert("schemaOmittedReason", "multiple_matches");
        object.insert("schemaQuery", tool.name);
      }
      result.push_back(std::move(object));
    };

  auto exactMatches = std::vector<std::reference_wrapper<const McpToolDefinition>>{};
  if (!normalizedQuery.isEmpty())
  {
    for (const auto& tool : defaultToolCatalog())
    {
      if (
        tool.implemented && allowsMode(mode, tool.requiredMode)
        && queryContainsExactToolName(normalizedQuery, tool.name)
        && (normalizedCategory.isEmpty() || tool.category.compare(normalizedCategory, Qt::CaseInsensitive) == 0))
      {
        exactMatches.push_back(std::cref(tool));
      }
    }
    if (exactMatches.empty() && queryHasAnyExactToolName(normalizedQuery))
    {
      for (const auto& tool : defaultToolCatalog())
      {
        if (
          tool.implemented && allowsMode(mode, tool.requiredMode)
          && queryContainsExactToolName(normalizedQuery, tool.name))
        {
          exactMatches.push_back(std::cref(tool));
        }
      }
    }
  }
  if (!exactMatches.empty())
  {
    auto result = QJsonArray{};
    for (const auto& tool : exactMatches)
    {
      appendTool(result, tool.get(), includeSchema);
    }
    return result;
  }

  auto nameFragmentMatches =
    std::vector<std::reference_wrapper<const McpToolDefinition>>{};
  if (!normalizedQuery.isEmpty())
  {
    for (const auto& tool : defaultToolCatalog())
    {
      if (
        tool.implemented && allowsMode(mode, tool.requiredMode)
        && tool.name.toLower().contains(normalizedQuery)
        && (normalizedCategory.isEmpty() || tool.category.compare(normalizedCategory, Qt::CaseInsensitive) == 0))
      {
        nameFragmentMatches.push_back(std::cref(tool));
      }
    }
  }
  if (nameFragmentMatches.size() == 1)
  {
    auto result = QJsonArray{};
    appendTool(result, nameFragmentMatches.front().get(), includeSchema);
    return result;
  }

  auto matches = std::vector<std::reference_wrapper<const McpToolDefinition>>{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (!tool.implemented || !allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    const auto exactNameMatch = !normalizedQuery.isEmpty()
                                && queryContainsExactToolName(normalizedQuery, tool.name);
    if (!visibleInProfile(tool, profile) && normalizedQuery.isEmpty())
    {
      continue;
    }
    if (
      !exactNameMatch && !normalizedCategory.isEmpty()
      && tool.category.compare(normalizedCategory, Qt::CaseInsensitive) != 0)
    {
      continue;
    }
    if (
      !exactNameMatch && !normalizedQuery.isEmpty()
      && !toolMatchesSearch(tool, tokens, includeSchema))
    {
      continue;
    }

    matches.push_back(std::cref(tool));
  }

  auto result = QJsonArray{};
  const auto includeToolSchema = includeSchema && matches.size() == 1;
  for (const auto& tool : matches)
  {
    appendTool(result, tool.get(), includeToolSchema);
  }
  return result;
}

} // namespace tb::mcp
