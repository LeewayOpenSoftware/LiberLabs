
# Backend: reads the network state, no user interface, reusable by other front ends
add_library(netcenter STATIC backend/netcenter.c)
target_include_directories(netcenter INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/backend)
target_include_directories(netcenter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend)
add_dependencies(netcenter psdk)

add_rc_deps(ncsc.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/ncsc.ico)
add_executable(ncsc ncsc.c ncsc.rc)
set_module_type(ncsc win32gui UNICODE)
target_link_libraries(ncsc netcenter)
add_importlibs(ncsc iphlpapi ws2_32 netapi32 advapi32 shell32 user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ncsc DESTINATION reactos/system32 FOR all)
