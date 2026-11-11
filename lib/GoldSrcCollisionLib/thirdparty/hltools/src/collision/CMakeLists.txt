cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(HltoolsCollision LANGUAGES CXX)
  include(CTest)
endif()

get_filename_component(HLTOOLS_COLLISION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
list(TRANSFORM HLTOOLS_COLLISION_SOURCES PREPEND "${HLTOOLS_COLLISION_ROOT}/")

add_library(HltoolsCollision STATIC ${HLTOOLS_COLLISION_SOURCES})
target_include_directories(HltoolsCollision SYSTEM PUBLIC "${HLTOOLS_COLLISION_ROOT}/src")
target_compile_features(HltoolsCollision PUBLIC cxx_std_17)
target_compile_definitions(HltoolsCollision PRIVATE
  HLTOOLS_DOUBLE_PRECISION HLTOOLS_EMBEDDED HLTOOLS_COLLISION_ONLY
  HLTOOLS_VERSION="embedded" HLTOOLS_COMMIT="embedded"
  _CRT_SECURE_NO_WARNINGS)
set_target_properties(HltoolsCollision PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(HltoolsCollision PRIVATE /fp:precise /EHsc)
else()
  target_compile_options(HltoolsCollision PRIVATE -ffp-contract=off -fno-fast-math)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
  add_executable(HltoolsCollisionTest test.cpp)
  target_link_libraries(HltoolsCollisionTest PRIVATE HltoolsCollision)
  add_test(NAME HltoolsCollision COMMAND HltoolsCollisionTest)
endif()
