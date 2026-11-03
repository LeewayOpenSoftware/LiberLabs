
add_definitions(-DXAUDIO2_VER=8)

set(XA ${CMAKE_CURRENT_SOURCE_DIR}/../xaudio2_9)

include_directories(
    ${XA}
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/faudio/include
    ${CMAKE_CURRENT_BINARY_DIR})

spec2def(xaudio2_8.dll xaudio2_8.spec)

add_idl_headers(xaudio2_8_idlheader ../xaudio2_9/xaudio_classes.idl)

add_library(xaudio2_8 MODULE
    ${XA}/x3daudio.c
    ${XA}/xapo.c
    ${XA}/xapofx.c
    ${XA}/xaudio_allocator.c
    ${XA}/xaudio_dll.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/xaudio2_8_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/xaudio2_8.def)

set_module_type(xaudio2_8 win32dll)
target_compile_definitions(xaudio2_8 PRIVATE __WINESRC__)
target_link_libraries(xaudio2_8 faudio mfuuid uuid wine)
add_importlibs(xaudio2_8 mfplat mfreadwrite propsys advapi32 ole32 user32 msvcrt kernel32 ntdll)
add_dependencies(xaudio2_8 xaudio2_8_idlheader)
add_cd_file(TARGET xaudio2_8 DESTINATION reactos/system32 FOR all)
set_wine_module(xaudio2_8)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_property(SOURCE
        ${XA}/xapo.c
        ${XA}/xapofx.c
        ${XA}/xaudio_dll.c
        APPEND PROPERTY COMPILE_OPTIONS -Wno-pragma-pack)
endif()
