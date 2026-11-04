
add_executable(liberx_item1 item1.c)
set_module_type(liberx_item1 win32cui)
add_dependencies(liberx_item1 psdk)
add_importlibs(liberx_item1 ole32 oleaut32 msvcrt kernel32)
