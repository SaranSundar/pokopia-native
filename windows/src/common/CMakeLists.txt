add_library(nxw_common STATIC common.cpp sha256.c)
target_include_directories(nxw_common PUBLIC include)
set_target_properties(nxw_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(nxw-common-tests-sha256 tests/t_sha256.c)
target_link_libraries(nxw-common-tests-sha256 PRIVATE nxw_common)
add_test(NAME common.sha256 COMMAND nxw-common-tests-sha256)
