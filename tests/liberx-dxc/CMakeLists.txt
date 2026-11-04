
add_executable(liberx_dxc dxc_diff.cpp)
set_module_type(liberx_dxc win32cui)
add_dependencies(liberx_dxc psdk)
target_include_directories(liberx_dxc PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/psdk)
target_link_libraries(liberx_dxc cpprt uuid)
add_importlibs(liberx_dxc msvcrt kernel32)
