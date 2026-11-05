
add_executable(liberx_contracts contracts.c)
set_module_type(liberx_contracts win32cui)
add_dependencies(liberx_contracts psdk)
add_importlibs(liberx_contracts msvcrt kernel32)

add_executable(liberx_d2d_geometry d2d-geometry.c)
set_module_type(liberx_d2d_geometry win32cui)
add_dependencies(liberx_d2d_geometry psdk)
add_importlibs(liberx_d2d_geometry msvcrt kernel32)

add_executable(liberx_dwrite_hittest dwrite-hittest.c)
set_module_type(liberx_dwrite_hittest win32cui)
add_dependencies(liberx_dwrite_hittest psdk)
add_importlibs(liberx_dwrite_hittest msvcrt kernel32 dwrite)

add_executable(liberx_input_contracts input-contracts.c)
set_module_type(liberx_input_contracts win32cui)
add_dependencies(liberx_input_contracts psdk)
add_importlibs(liberx_input_contracts msvcrt kernel32 dinput8 hid setupapi)

add_executable(liberx_xinput_battery xinput-battery.c)
set_module_type(liberx_xinput_battery win32cui)
add_dependencies(liberx_xinput_battery psdk)
add_importlibs(liberx_xinput_battery msvcrt kernel32)

add_executable(liberx_dxgi_priority dxgi-priority.c)
set_module_type(liberx_dxgi_priority win32cui)
add_dependencies(liberx_dxgi_priority psdk)
add_importlibs(liberx_dxgi_priority msvcrt kernel32 d3d11 dxgi)

add_executable(liberx_d3d11_eviction d3d11-eviction.c)
set_module_type(liberx_d3d11_eviction win32cui)
add_dependencies(liberx_d3d11_eviction psdk)
add_importlibs(liberx_d3d11_eviction msvcrt kernel32 d3d11)
