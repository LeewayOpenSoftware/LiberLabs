
add_definitions(-DXAPOFX1_VER=5 -DXAUDIO2_VER=7)

set(XA ${CMAKE_CURRENT_SOURCE_DIR}/../xaudio2_9)

include_directories(
    ${XA}
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/faudio/include
    ${CMAKE_CURRENT_BINARY_DIR})

spec2def(xapofx1_5.dll xapofx1_5.spec)

add_idl_headers(xapofx1_5_idlheader ../xaudio2_9/xaudio_classes.idl)

add_library(xapofx1_5 MODULE
    ${XA}/xapo.c
    ${XA}/xapofx.c
    ${XA}/xaudio_allocator.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/xapofx1_5_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/xapofx1_5.def)

set_module_type(xapofx1_5 win32dll)
target_compile_definitions(xapofx1_5 PRIVATE __WINESRC__)
target_link_libraries(xapofx1_5 faudio uuid wine)
add_importlibs(xapofx1_5 ole32 msvcrt kernel32 ntdll)
add_dependencies(xapofx1_5 xapofx1_5_idlheader)
add_cd_file(TARGET xapofx1_5 DESTINATION reactos/system32 FOR all)
set_wine_module(xapofx1_5)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_property(SOURCE
        ${XA}/xapo.c
        ${XA}/xapofx.c
        APPEND PROPERTY COMPILE_OPTIONS -Wno-pragma-pack)
endif()
