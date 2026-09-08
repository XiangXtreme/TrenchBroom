add_executable(DumpPythonApi
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp
)

target_link_libraries(DumpPythonApi
  PRIVATE
    CompilerConfig
    Python3::Python
    TbUiLib
    Qt6::Core
    pybind11::embed
)

EMBED_UTF8_MANIFEST(DumpPythonApi)
