add_executable(TbCsLibTest)
EMBED_UTF8_MANIFEST(TbCsLibTest)

target_sources(TbCsLibTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_ScsApi.cpp
)

target_link_libraries(TbCsLibTest
  PRIVATE
    CompilerConfig
    Catch2::Catch2WithMain
    TbCsLib
)

include(Catch)
catch_discover_tests(TbCsLibTest)
