# TbCsLib: embedded Counter-Strike 1.6 player movement for play mode.
#
# The movement code itself is ReGameDLL_CS's pm_shared (MIT), compiled unmodified
# from thirdparty/regamedll with the REGAMEDLL_* extensions off, i.e. the original
# CS 1.6 behaviour. TbCsLib provides what the game engine would around it: GoldSrc
# clip hulls compiled from brushes in memory, hull traces and the per-command
# player setup.

set(TB_CS_GAME_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/regamedll/common
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/regamedll/dlls
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/regamedll/pm_shared
)

# Bit-for-bit float behaviour like the engine: no FMA contraction, no fast-math.
if(MSVC)
  set(TB_CS_FP_FLAGS /fp:precise)
else()
  set(TB_CS_FP_FLAGS -ffp-contract=off -fno-fast-math)
endif()

# --- pm_shared, compiled unmodified ---------------------------------------------
add_library(TbCsLibPmShared OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/regamedll/pm_shared/pm_shared.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/regamedll/pm_shared/pm_math.cpp
)
# pm_shared.cpp includes "precompiled.h", which src/regamedll provides.
target_include_directories(TbCsLibPmShared PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/regamedll
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${TB_CS_GAME_INCLUDES}
)
target_compile_features(TbCsLibPmShared PRIVATE cxx_std_17)
set_target_properties(TbCsLibPmShared PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
)
target_compile_options(TbCsLibPmShared PRIVATE ${TB_CS_FP_FLAGS})
if(MSVC)
  # The game code passes string literals to char* parameters.
  target_compile_options(TbCsLibPmShared PRIVATE /w /Zc:strictStrings-)
else()
  target_compile_options(TbCsLibPmShared PRIVATE -w)
endif()

# --- TbCsLib ---------------------------------------------------------------------
add_library(TbCsLib STATIC)

target_sources(TbCsLib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BspTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Hlsdk.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayerHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayerHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PmTrace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PmTraceEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ReGameDllCompat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ScsApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/World.h
    $<TARGET_OBJECTS:TbCsLibPmShared>

  PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/cs/scs.h
)

target_include_directories(TbCsLib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Third-party headers: their warnings are not ours to fix.
target_include_directories(TbCsLib SYSTEM PRIVATE ${TB_CS_GAME_INCLUDES})
target_compile_options(TbCsLib PRIVATE ${TB_CS_FP_FLAGS})
set_target_properties(TbCsLib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(TbCsLib PRIVATE CompilerConfig)
if(UNIX)
  target_link_libraries(TbCsLib PUBLIC m)
endif()

add_subdirectory(test)
