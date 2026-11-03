
add_definitions(-DX3DAUDIO1_VER=7 -DXAUDIO2_VER=7)

set(XA ${CMAKE_CURRENT_SOURCE_DIR}/../xaudio2_9)

include_directories(
    ${XA}
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/faudio/include
    ${CMAKE_CURRENT_BINARY_DIR})

spec2def(x3daudio1_7.dll x3daudio1_7.spec)

add_library(x3daudio1_7 MODULE
    ${XA}/x3daudio.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/x3daudio1_7_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/x3daudio1_7.def)

set_module_type(x3daudio1_7 win32dll)
target_compile_definitions(x3daudio1_7 PRIVATE __WINESRC__)
target_link_libraries(x3daudio1_7 faudio uuid wine)
add_importlibs(x3daudio1_7 msvcrt kernel32 ntdll)
add_cd_file(TARGET x3daudio1_7 DESTINATION reactos/system32 FOR all)
set_wine_module(x3daudio1_7)
