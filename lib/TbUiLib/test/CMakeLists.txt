add_executable(TbUiLibTest)
EMBED_UTF8_MANIFEST(TbUiLibTest)

target_sources(TbUiLibTest
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RunAllTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ActionContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ActionInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_Actions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_AssetBrowserModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_AssetPreviewProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CompilationDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CompilationProfileManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CompilationRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CrashReporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CompilationTaskListBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CellView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_CollapsibleTitledPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ViewEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_EntityPropertyModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_GoldSrcSpritePreview.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_InputEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_LaunchGameEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_MapViewToolBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_MapWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_MaterialBrowserView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_MiscPreferencePane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ModelBrowserView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpToolRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpPythonExecution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_OutlinerEntityPropertyEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_OutlinerTreeWidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PieMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PieMenuSettingsDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PopupButton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PreferenceDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PrefabAsset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PrefabTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PythonConsole.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PythonCompletionEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PluginInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PythonPluginManifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PythonApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QPathUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QPreferenceStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QPreferenceStoreUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QStringUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QVecUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_QWidgetUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_PathTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_RecentDocuments.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_SmartFaceSelectionPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_SmartModelEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_SmartSkyboxEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_KeyboardShortcutUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_LayerListBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_SystemPaths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_TabBook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_TextOutputAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_UiSnapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_UpdateVersion.cpp

  PUBLIC FILE_SET cmd_tool TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
)

add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

target_link_libraries(TbUiLibTest PRIVATE
  CompilerConfig
  PrecompileQtHeaders
  PrecompileStdHeaders
  Catch2::Catch2WithMain
  Qt6::Multimedia
  Qt6::Network
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Svg
  Qt6::Test
  Qt6::Widgets
  Python3::Python
  pybind11::embed
  TbAppTestUtilsLib
  TbBaseTestUtilsLib
  TbElLib
  TbElTestUtilsLib
  TbFsTestUtilsLib
  TbMcpLib
  TbMdlTestUtilsLib
  TbPreferencesLib
  TbRenderLib
  TbUiLib
  TbUiTestUtilsLib
  UpdateLib
)

set_target_properties(TbUiLibTest PROPERTIES AUTOMOC TRUE)

add_dependencies(TbUiLibTest CmdTool)
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/CmdTool.h"
  CONTENT "#define CMD_TOOL_PATH \"$<TARGET_FILE:CmdTool>\"")

# Copy fixtures
set(TEST_FIXTURE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixture)
set(TEST_FIXTURE_DEST_DIR $<TARGET_FILE_DIR:TbUiLibTest>/fixture)

add_custom_command(TARGET TbUiLibTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${TEST_FIXTURE_DEST_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TEST_FIXTURE_SOURCE_DIR}" "${TEST_FIXTURE_DEST_DIR}/test"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TB_RESOURCE_DIR}/games" "${TEST_FIXTURE_DEST_DIR}/games"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TB_RESOURCE_DIR}/games-testing" "${TEST_FIXTURE_DEST_DIR}/games"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${TB_RESOURCE_DIR}/graphics/images" "$<TARGET_FILE_DIR:TbUiLibTest>/images"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/python/examples" "$<TARGET_FILE_DIR:TbUiLibTest>/python/examples"
)

include(Catch)
catch_discover_tests(TbUiLibTest)

# McpPythonExecution creates a full editor fixture per section. Register each
# section separately so a stalled integration path has a short, visible timeout.
set(TB_MCP_PYTHON_EXECUTION_SECTIONS
  "API discovery has complete bounded pages and actual binding metadata"
  "problem summaries omit ignored issues and details page within the byte budget"
  "viewport options are idempotent and invalid patches have no side effects"
  "property edits preserve handles and selection through delete and history"
  "cached editor handles reject worker thread access"
  "foreign document handles cannot escape the guarded transaction"
  "viewport changes are synchronous actions with truthful receipts"
  "large replay payloads expire without repeating edits"
  "worldspawn property edits preserve selection before subsequent brush edits"
  "action failure before editing has no mutation or completed actions"
  "action failures preserve edits for every failure exit"
  "undo followed by failure records the completed native action"
  "undo and a replacement edit cannot hide mutation behind the same count"
  "save followed by failure retains persistence evidence without a map edit"
  "closing the target followed by failure retains lifecycle evidence"
  "transaction failure restores preexisting selection and dirty state"
  "native linked group commit failure rolls back the script"
  "replaying a completed edit does not repeat the edit"
)

foreach(section IN LISTS TB_MCP_PYTHON_EXECUTION_SECTIONS)
  add_test(
    NAME "McpPythonExecution: ${section}"
    COMMAND TbUiLibTest McpPythonExecution --section "${section}"
  )
  set_tests_properties(
    "McpPythonExecution: ${section}"
    PROPERTIES
      WORKING_DIRECTORY "$<TARGET_FILE_DIR:TbUiLibTest>"
      RESOURCE_LOCK McpPythonExecution
      TIMEOUT 60
  )
endforeach()
