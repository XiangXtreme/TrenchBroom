add_library(TbMcpLib STATIC)

target_sources(TbMcpLib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpBridgeClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpBridgeConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpBridgeMessages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpError.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpJsonRpc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpMode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpToolCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/McpToolCatalogSerialization.cpp

  PUBLIC FILE_SET headers TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(TbMcpLib
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
  PUBLIC
    Qt6::Core
    Qt6::Network
)

add_subdirectory(test)
