add_executable(TbProjectLibTest)
EMBED_UTF8_MANIFEST(TbProjectLibTest)

target_sources(TbProjectLibTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_AssetLibrary.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ContentHash.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_GoldSrcModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_GoldSrcWad.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_LibrarySync.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ProjectStore.cpp
)

target_link_libraries(TbProjectLibTest PRIVATE
  CompilerConfig
  PrecompileStdHeaders
  Catch2::Catch2WithMain
  TbProjectLib
  TbSqlite
)

include(Catch)
catch_discover_tests(TbProjectLibTest)
