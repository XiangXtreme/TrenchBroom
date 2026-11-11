cmake_minimum_required(VERSION 3.21)

# The parent supplies SQLite. No ZIP, GPU SDK or network dependency is needed.
if(NOT TARGET TbSqlite AND NOT TARGET hltools_sqlite)
  message(FATAL_ERROR "The preview worker requires a parent-provided SQLite target")
endif()
get_filename_component(HLTOOLS_PREVIEW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
find_package(Threads REQUIRED)
foreach(group common format csg bsp vis rad driver)
  string(TOUPPER "${group}" upper)
  set(sources ${HLTOOLS_PREVIEW_${upper}_SOURCES})
  list(TRANSFORM sources PREPEND "${HLTOOLS_PREVIEW_ROOT}/")
  add_library(HltoolsPreview_${group} STATIC ${sources})
  target_include_directories(HltoolsPreview_${group} SYSTEM PUBLIC "${HLTOOLS_PREVIEW_ROOT}/src")
  target_compile_features(HltoolsPreview_${group} PUBLIC cxx_std_17)
  target_compile_definitions(HltoolsPreview_${group} PRIVATE
    HLTOOLS_PREVIEW_ONLY HLTOOLS_VERSION="preview" HLTOOLS_COMMIT="bundled"
    _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(HltoolsPreview_${group} PRIVATE /W3 /EHsc /fp:precise)
  else()
    target_compile_options(HltoolsPreview_${group} PRIVATE -O2 -fno-strict-aliasing)
  endif()
  if(group MATCHES "^(csg|bsp|driver)$")
    target_compile_definitions(HltoolsPreview_${group} PRIVATE HLTOOLS_DOUBLE_PRECISION)
  endif()
endforeach()
# This common module stores CSG's double-precision direct trees.
set_source_files_properties("${HLTOOLS_PREVIEW_ROOT}/src/common/direct_clip_io.cpp"
  PROPERTIES COMPILE_DEFINITIONS HLTOOLS_DOUBLE_PRECISION)
if(TARGET TbSqlite)
  target_link_libraries(HltoolsPreview_format PUBLIC TbSqlite)
else()
  target_link_libraries(HltoolsPreview_format PUBLIC hltools_sqlite)
endif()
target_link_libraries(HltoolsPreview_common PUBLIC Threads::Threads)
foreach(group format csg bsp vis rad)
  target_link_libraries(HltoolsPreview_${group} PUBLIC HltoolsPreview_common)
endforeach()
foreach(group csg bsp vis rad)
  target_link_libraries(HltoolsPreview_${group} PUBLIC HltoolsPreview_format)
endforeach()
set_target_properties(HltoolsPreview_rad HltoolsPreview_vis
  PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_link_libraries(HltoolsPreview_driver PUBLIC
  HltoolsPreview_csg HltoolsPreview_bsp HltoolsPreview_vis HltoolsPreview_rad)
add_executable(HltoolsLightmapWorker main.cpp)
target_link_libraries(HltoolsLightmapWorker PRIVATE HltoolsPreview_driver)

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME HltoolsLightmapWorker
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_cli_parity.py"
        --worker $<TARGET_FILE:HltoolsLightmapWorker>)
    set_tests_properties(HltoolsLightmapWorker PROPERTIES TIMEOUT 180)
  endif()
endif()
