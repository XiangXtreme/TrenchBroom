include("${CMAKE_CURRENT_LIST_DIR}/../Utils.cmake")

function(check_version description expected_version expected_patch expected_rc)
    set(git_description "${description}")
    get_app_version(git_description year number patch rc)
    get_app_version_str(year number patch rc version)
    if(NOT "${version}" STREQUAL "${expected_version}" OR
       NOT "${patch}" STREQUAL "${expected_patch}" OR
       NOT "${rc}" STREQUAL "${expected_rc}")
        message(FATAL_ERROR "${description}: got ${version}, patch=${patch}, rc=${rc}")
    endif()
endfunction()

check_version("v2026.10" "2026.10" "0" "")
check_version("v2026.10-RC2" "2026.10-RC2" "0" "2")
check_version("v2026.10.1" "2026.10.1" "1" "")
check_version("v2026.10.1-RC2" "2026.10.1-RC2" "1" "2")
check_version("v2026.10.1-12-gabc123" "2026.10.1" "1" "")
check_version("v2026.10.1-12-gabc123-dirty" "2026.10.1" "1" "")
check_version("v2026.10.1-RC2-12-gabc123" "2026.10.1-RC2" "1" "2")
check_version("v2026.10-dirty" "2026.10" "0" "")
check_version("v2026.10.1.2" "0.0" "0" "")
check_version("unknown" "0.0" "0" "")

# Repeated calls must clear the patch and RC fields from the previous version.
set(git_description "v2026.10.1-RC2")
get_app_version(git_description year number patch rc)
set(git_description "v2026.10")
get_app_version(git_description year number patch rc)
get_app_version_str(year number patch rc version)
if(NOT "${version}" STREQUAL "2026.10" OR NOT "${patch}" STREQUAL "0" OR NOT "${rc}" STREQUAL "")
    message(FATAL_ERROR "Version fields leaked from the previous call")
endif()
