# Official amalgamation, pinned and verified against sqlite.org/download.html.
# Keep this independent of Qt so project storage can also be used by hltools.
CPMAddPackage(
  NAME sqlite
  VERSION 3.54.1
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
  DOWNLOAD_ONLY YES
)

add_library(TbSqlite STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(TbSqlite SYSTEM PUBLIC "${sqlite_SOURCE_DIR}")
target_compile_definitions(TbSqlite PRIVATE SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(TbSqlite PRIVATE ${CMAKE_DL_LIBS})
if(UNIX)
  target_link_libraries(TbSqlite PRIVATE m)
endif()
suppress_dependency_warnings(TbSqlite)
apply_sanitizer_options(TbSqlite)
