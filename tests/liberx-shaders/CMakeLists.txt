
add_executable(liberx_shaders shaders.c)
set_module_type(liberx_shaders win32cui)
add_dependencies(liberx_shaders psdk)
add_importlibs(liberx_shaders msvcrt kernel32)
