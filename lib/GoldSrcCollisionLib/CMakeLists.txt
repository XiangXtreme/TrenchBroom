# GoldSrcCollisionLib: the shared, in-memory GoldSrc clip-hull compiler.
#
# This target has no Qt, editor, or player-movement dependency. The hltools
# backend compiles a pinned snapshot of the upstream CSG/BSP source; the older
# plane-based VHLT/ZHLT backends remain available through HullCompiler.h.

# Verify the pinned source on every configure; edits belong in hltools and must
# be imported through scripts/sync-hltools-collision.py.
set(HLTOOLS_SNAPSHOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hltools")
file(READ "${HLTOOLS_SNAPSHOT}/manifest.json" HLTOOLS_MANIFEST)
string(JSON HLTOOLS_FILE_COUNT LENGTH "${HLTOOLS_MANIFEST}" files)
math(EXPR HLTOOLS_LAST_FILE "${HLTOOLS_FILE_COUNT} - 1")
foreach(index RANGE ${HLTOOLS_LAST_FILE})
  string(JSON name MEMBER "${HLTOOLS_MANIFEST}" files ${index})
  string(JSON expected GET "${HLTOOLS_MANIFEST}" files "${name}")
  file(SHA256 "${HLTOOLS_SNAPSHOT}/${name}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Modified hltools source: ${name}. Update upstream and sync the snapshot.")
  endif()
endforeach()

add_subdirectory(thirdparty/hltools/src/collision)
add_subdirectory(thirdparty/hltools/src/preview)

# Run deployment even when only the worker changed and the consumer does not link.
function(tb_deploy_lightmap_worker target)
  add_custom_target(${target}-lightmap-worker
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:HltoolsLightmapWorker>" "$<TARGET_FILE_DIR:${target}>"
    DEPENDS HltoolsLightmapWorker
    VERBATIM)
  add_dependencies(${target} ${target}-lightmap-worker)
  # Refresh bundle deployment/signatures after a worker update on Make/Ninja.
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:HltoolsLightmapWorker>")
endfunction()

add_library(GoldSrcCollisionLib STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/BspTypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HltoolsCompiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HltoolsCompiler.h
)

target_include_directories(GoldSrcCollisionLib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(GoldSrcCollisionLib PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(GoldSrcCollisionLib PRIVATE /fp:precise)
else()
  target_compile_options(GoldSrcCollisionLib PRIVATE -ffp-contract=off -fno-fast-math)
endif()
target_link_libraries(GoldSrcCollisionLib PRIVATE CompilerConfig HltoolsCollision)
set_target_properties(GoldSrcCollisionLib PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX)
  target_link_libraries(GoldSrcCollisionLib PUBLIC m)
endif()
