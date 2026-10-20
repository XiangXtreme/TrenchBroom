# Windows executables embed CPython and must carry its matching standard library.
# A DLL alone can accidentally work by finding the developer's Python in the registry.
function(tb_deploy_python_runtime target)
    if(NOT WIN32)
        return()
    endif()

    get_filename_component(TB_PYTHON_PREFIX "${Python3_EXECUTABLE}" DIRECTORY)
    set(TB_PYTHON_STDLIB "${TB_PYTHON_PREFIX}/Lib")
    set(TB_PYTHON_RUNTIME_LIBRARY
        "${TB_PYTHON_PREFIX}/python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}.dll")
    if(NOT EXISTS "${TB_PYTHON_STDLIB}/encodings/__init__.py"
       OR NOT EXISTS "${TB_PYTHON_RUNTIME_LIBRARY}")
        message(FATAL_ERROR "A complete Windows Python runtime is required for ${target}")
    endif()
    set(TB_PYTHON_RUNTIME_DIR "${TB_PYTHON_PREFIX}")
    set(deploy_script "${CMAKE_CURRENT_BINARY_DIR}/${target}-DeployPythonRuntime.cmake")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/DeployPythonRuntime.cmake.in" "${deploy_script}" @ONLY)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DTB_PYTHON_DEST=$<TARGET_FILE_DIR:${target}>" -P "${deploy_script}"
        VERBATIM)
endfunction()
