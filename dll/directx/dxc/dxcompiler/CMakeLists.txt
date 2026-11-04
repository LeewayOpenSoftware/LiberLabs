# PROJECT:     LiberX DirectX Shader Compiler (DXC) compatibility layer
# LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
# PURPOSE:     Item 18 DXC Level 1 - dxcompiler.dll exposing IDxcCompiler3 /
#              IDxcUtils / IDxcValidator. Explicitly limited; never advertises
#              full Shader Model 6. SM6 compile fails with a defined HRESULT.
#              B0: COM surface + honest E_NOTIMPL. B1 links vkd3d_compiler for
#              the real parse/validate/reflect/disassemble + HLSL->SM<=5 paths.
# COPYRIGHT:   Copyright 2026 Leeway Open Software
#                <281492504+LeewayOpenSoftware@users.noreply.github.com>

spec2def(dxcompiler.dll dxcompiler.spec ADD_IMPORTLIB)

add_library(dxcompiler MODULE
    dxcompiler.cpp
    dxcompiler.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxcompiler.def)

set_module_type(dxcompiler win32dll ENTRYPOINT 0)
target_include_directories(dxcompiler PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk)
target_link_libraries(dxcompiler cpprt uuid)
add_importlibs(dxcompiler ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dxcompiler DESTINATION reactos/system32 FOR all)
