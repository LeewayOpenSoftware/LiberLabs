
add_executable(liberx_contracts contracts.c)
set_module_type(liberx_contracts win32cui)
add_dependencies(liberx_contracts psdk)
add_importlibs(liberx_contracts msvcrt kernel32)
