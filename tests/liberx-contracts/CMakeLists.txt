
add_executable(liberx_contracts contracts.c)
set_module_type(liberx_contracts win32cui)
add_dependencies(liberx_contracts psdk)
add_importlibs(liberx_contracts msvcrt kernel32)

add_executable(liberx_d2d_geometry d2d-geometry.c)
set_module_type(liberx_d2d_geometry win32cui)
add_dependencies(liberx_d2d_geometry psdk)
add_importlibs(liberx_d2d_geometry msvcrt kernel32)
