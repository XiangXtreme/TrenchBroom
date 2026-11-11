cmake_minimum_required(VERSION 3.21)

# The parent supplies SQLite. GPU support is optional and uses an installed
# Vulkan SDK; the worker keeps its CPU fallback when the SDK is unavailable.
if(NOT TARGET TbSqlite AND NOT TARGET hltools_sqlite)
  message(FATAL_ERROR "The preview worker requires a parent-provided SQLite target")
endif()
get_filename_component(HLTOOLS_PREVIEW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
find_package(Threads REQUIRED)

option(HLTOOLS_PREVIEW_GPU "Build the preview worker Vulkan lighting backend" ON)
if(HLTOOLS_PREVIEW_GPU)
  file(GLOB NDK_SHADER_TOOLS
    "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/*/shader-tools/windows-x86_64"
    "$ENV{HOME}/Android/Sdk/ndk/*/shader-tools/linux-x86_64")
  find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" ${NDK_SHADER_TOOLS})
  find_path(VULKAN_HEADERS_INCLUDE_DIR vulkan/vulkan.h
    HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
  if(NOT GLSLC_EXECUTABLE OR NOT VULKAN_HEADERS_INCLUDE_DIR)
    message(STATUS "HLTOOLS_PREVIEW_GPU: Vulkan SDK/glslc not found; using CPU lighting")
    set(HLTOOLS_PREVIEW_GPU OFF)
  endif()
endif()

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

if(HLTOOLS_PREVIEW_GPU)
  set(RAD_GPU_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/rad_gpu")
  set(RAD_GPU_SHADER_DIR "${HLTOOLS_PREVIEW_ROOT}/src/rad/gpu/shaders")
  set(RAD_GPU_EMBED_SCRIPT "${HLTOOLS_PREVIEW_ROOT}/cmake/embed_binary.cmake")
  function(hltools_preview_gpu_kernel SRC NAME SYMBOL)
    add_custom_command(
      OUTPUT "${RAD_GPU_GEN_DIR}/${NAME}_spirv.h"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${RAD_GPU_GEN_DIR}"
      COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.0 -O ${ARGN}
        -o "${RAD_GPU_GEN_DIR}/${NAME}.spv" "${RAD_GPU_SHADER_DIR}/${SRC}.comp"
      COMMAND ${CMAKE_COMMAND}
        "-DINPUT=${RAD_GPU_GEN_DIR}/${NAME}.spv"
        "-DOUTPUT=${RAD_GPU_GEN_DIR}/${NAME}_spirv.h"
        "-DSYMBOL=${SYMBOL}"
        -P "${RAD_GPU_EMBED_SCRIPT}"
      DEPENDS "${RAD_GPU_SHADER_DIR}/${SRC}.comp"
        "${RAD_GPU_SHADER_DIR}/trace_lib.glsl" "${RAD_GPU_EMBED_SCRIPT}"
      COMMENT "glslc: ${SRC}.comp -> ${NAME} SPIR-V"
      VERBATIM)
  endfunction()

  hltools_preview_gpu_kernel(trace_bsp trace_bsp g_trace_bsp_spirv)
  hltools_preview_gpu_kernel(gather gather g_gather_spirv)
  hltools_preview_gpu_kernel(gather gather_f32 g_gather_f32_spirv -DFLOAT_NORMALIZE)
  hltools_preview_gpu_kernel(formfactor formfactor g_formfactor_spirv)

  add_library(HltoolsPreview_gpu STATIC
    "${HLTOOLS_PREVIEW_ROOT}/src/rad/gpu/gpu.cpp"
    "${RAD_GPU_GEN_DIR}/trace_bsp_spirv.h"
    "${RAD_GPU_GEN_DIR}/gather_spirv.h"
    "${RAD_GPU_GEN_DIR}/gather_f32_spirv.h"
    "${RAD_GPU_GEN_DIR}/formfactor_spirv.h")
  target_include_directories(HltoolsPreview_gpu
    PUBLIC "${HLTOOLS_PREVIEW_ROOT}/src"
    PRIVATE "${RAD_GPU_GEN_DIR}")
  target_include_directories(HltoolsPreview_gpu SYSTEM PRIVATE "${VULKAN_HEADERS_INCLUDE_DIR}")
  target_compile_features(HltoolsPreview_gpu PUBLIC cxx_std_17)
  target_link_libraries(HltoolsPreview_gpu PUBLIC HltoolsPreview_common)
  if(NOT WIN32)
    target_link_libraries(HltoolsPreview_gpu PRIVATE ${CMAKE_DL_LIBS})
  endif()
  target_compile_definitions(HltoolsPreview_rad PRIVATE HLTOOLS_GPU_ENABLED)
  target_link_libraries(HltoolsPreview_rad PUBLIC HltoolsPreview_gpu)
endif()
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

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL HLTOOLS_PREVIEW_ROOT)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME HltoolsLightmapWorker
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_cli_parity.py"
        --worker $<TARGET_FILE:HltoolsLightmapWorker> --cli $<TARGET_FILE:hltools>)
    add_test(NAME HltoolsLightmapWorkerGpu
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_cli_parity.py"
        --worker $<TARGET_FILE:HltoolsLightmapWorker> --cli $<TARGET_FILE:hltools> --gpu)
    set_tests_properties(HltoolsLightmapWorker HltoolsLightmapWorkerGpu PROPERTIES
      TIMEOUT 180 RESOURCE_LOCK HltoolsLightmapWorker)
  endif()
endif()
