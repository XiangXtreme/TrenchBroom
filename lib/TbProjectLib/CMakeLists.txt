add_library(TbProjectLib STATIC)

target_sources(TbProjectLib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AssetLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ContentHash.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GoldSrcWad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LibrarySync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectStore.cpp
  PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(TbProjectLib
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    TbSqlite
  PUBLIC
    TbBaseLib
)

add_subdirectory(test)
