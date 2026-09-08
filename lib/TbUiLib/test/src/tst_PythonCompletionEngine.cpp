#include "ui/python/PythonCompletionEngine.h"

#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PythonCompletionEngine")
{
  const auto brushType = PythonApiValueType{PythonApiType::Brush};
  const auto brushListType = PythonApiValueType{PythonApiType::Brush, 1u};
  const auto entityType = PythonApiValueType{PythonApiType::Entity};
  const auto faceType = PythonApiValueType{PythonApiType::Face};
  const auto vec3Type = PythonApiValueType{PythonApiType::Vec3};

  SECTION("resolves object and sequence chains")
  {
    CHECK(pythonCompletionTypeForExpression("sel.brush") == brushType);
    CHECK(pythonCompletionTypeForExpression("sel.brush.entity") == entityType);
    CHECK(pythonCompletionTypeForExpression("sel.brush.entity.brushes") == brushListType);
    CHECK(pythonCompletionTypeForExpression("sel.brush.entity.brushes[0]") == brushType);
    CHECK_FALSE(pythonCompletionTypeForExpression("sel.brush.entity.brushes.entity"));
  }

  SECTION("requires indexing before completing sequence elements")
  {
    CHECK(pythonCompletionTypeForExpression("sel.brushes") == brushListType);
    CHECK(pythonCompletionTypeForExpression("sel.brushes[0]") == brushType);
    CHECK(
      pythonCompletionTypeForExpression("sel.brushes[0].faces()[0]") == faceType);
    CHECK(pythonCompletionTypeForExpression("sel.brush_vertices()[0][0]") == vec3Type);
    CHECK_FALSE(pythonCompletionTypeForExpression("sel.brushes.entity"));
  }

  SECTION("resolves module calls and rejects unknown members")
  {
    CHECK(
      pythonCompletionTypeForExpression("trenchbroom.documents.current().selection.brush")
      == brushType);
    CHECK(
      pythonCompletionTypeForExpression(
        "Plane.from_points((0, 0, 0), (1, 0, 0), (0, 1, 0))")
      == PythonApiValueType{PythonApiType::Plane});
    CHECK(pythonCompletionTypeForExpression("Vec3(1, 2, 3).normalized()") == vec3Type);
    CHECK_FALSE(pythonCompletionTypeForExpression("foo"));
    CHECK_FALSE(pythonCompletionTypeForExpression("doc.unknown"));
    CHECK_FALSE(pythonCompletionTypeForExpression("doc.entities.entity"));
  }

  SECTION("uses runtime roots without overriding known unsupported values")
  {
    const auto provider = PythonCompletionRootProvider{[](const std::string_view name) {
      if (name == "api")
      {
        return PythonCompletionRoot{true, PythonApiValueType{PythonApiType::Module}};
      }
      if (name == "x")
      {
        return PythonCompletionRoot{true, PythonApiValueType{PythonApiType::Brush}};
      }
      if (name == "e")
      {
        return PythonCompletionRoot{true, std::nullopt};
      }
      return PythonCompletionRoot{};
    }};

    CHECK(
      pythonCompletionTypeForExpression("api.documents.current().selection", provider)
      == PythonApiValueType{PythonApiType::Selection});
    CHECK(pythonCompletionTypeForExpression("x.entity", provider) == entityType);
    CHECK_FALSE(pythonCompletionTypeForExpression("e", provider));
    CHECK(pythonCompletionTypeForExpression("doc.selection", provider));
  }
}

} // namespace tb::ui
