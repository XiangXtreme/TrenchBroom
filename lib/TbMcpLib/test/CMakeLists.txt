add_executable(TbMcpLibTest)

target_sources(TbMcpLibTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpBridgeConfig.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpBridgeMessages.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpJsonRpc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpMode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpToolCatalog.cpp
)

add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

target_link_libraries(TbMcpLibTest
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    Catch2::Catch2WithMain
    Qt6::Core
    TbMcpLib
)

include(Catch)
catch_discover_tests(TbMcpLibTest)

add_executable(TbMcpStdioClientTest)

target_sources(TbMcpStdioClientTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_McpBridgeClient.cpp
)

target_link_libraries(TbMcpStdioClientTest
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    Catch2::Catch2WithMain
    Qt6::Core
    TbMcpLib
)

catch_discover_tests(TbMcpStdioClientTest)
