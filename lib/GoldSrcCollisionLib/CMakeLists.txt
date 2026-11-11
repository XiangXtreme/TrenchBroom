# GoldSrcCollisionLib: the shared, in-memory GoldSrc clip-hull compiler.
#
# This target deliberately has no Qt, editor, or player-movement dependency. It
# is the collision core used by TbCsLib today and is kept as a separate target
# so the hltools compiler can consume the same implementation without creating
# a second hull-expansion/BSP implementation.

add_library(GoldSrcCollisionLib STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/BspTypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/HullCompiler.h
)

target_include_directories(GoldSrcCollisionLib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(GoldSrcCollisionLib PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(GoldSrcCollisionLib PRIVATE /fp:precise)
else()
  target_compile_options(GoldSrcCollisionLib PRIVATE -ffp-contract=off -fno-fast-math)
endif()
target_link_libraries(GoldSrcCollisionLib PRIVATE CompilerConfig)
set_target_properties(GoldSrcCollisionLib PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(UNIX)
  target_link_libraries(GoldSrcCollisionLib PUBLIC m)
endif()
