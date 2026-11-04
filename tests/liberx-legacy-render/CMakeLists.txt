
add_executable(liberx_legacy_render legacy_render.c d3d8_render.c)
set_module_type(liberx_legacy_render win32cui)
add_dependencies(liberx_legacy_render psdk)
add_importlibs(liberx_legacy_render msvcrt kernel32 user32 gdi32)
