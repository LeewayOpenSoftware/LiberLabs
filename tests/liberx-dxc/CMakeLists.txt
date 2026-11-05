
add_executable(liberx_dxc dxc_diff.cpp)
set_module_type(liberx_dxc win32cui)
add_dependencies(liberx_dxc psdk)
target_include_directories(liberx_dxc PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/psdk)
target_link_libraries(liberx_dxc cpprt uuid)
add_importlibs(liberx_dxc msvcrt kernel32)

# Level 2: drives dxcompiler.dll over the sm6/ fixture set (sm6/CASES.txt).
add_executable(liberx_dxc_sm6 dxc_sm6.cpp)
set_module_type(liberx_dxc_sm6 win32cui)
add_dependencies(liberx_dxc_sm6 psdk)
target_include_directories(liberx_dxc_sm6 PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/psdk)
target_link_libraries(liberx_dxc_sm6 cpprt uuid)
add_importlibs(liberx_dxc_sm6 msvcrt kernel32)
