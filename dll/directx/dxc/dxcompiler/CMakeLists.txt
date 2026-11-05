# PROJECT:     LiberX DirectX Shader Compiler (DXC) compatibility layer
# LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
# PURPOSE:     Item 18 DXC Level 1 - dxcompiler.dll exposing IDxcCompiler3 /
#              IDxcUtils / IDxcValidator. Explicitly limited; never advertises
#              full Shader Model 6. SM6 compile fails with a defined HRESULT.
#              Backed by vkd3d-shader for HLSL->SM<=5 compile, DXBC/DXIL
#              disassembly, and structural (unsigned) DXIL validation.
# COPYRIGHT:   Copyright 2026 Leeway Open Software
#                <281492504+LeewayOpenSoftware@users.noreply.github.com>

spec2def(dxcompiler.dll dxcompiler.spec ADD_IMPORTLIB)

add_library(dxcompiler MODULE
    dxcompiler.cpp
    dxcompiler.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxcompiler.def)

set_module_type(dxcompiler win32dll ENTRYPOINT 0)
target_include_directories(dxcompiler PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/vkd3d/include)
target_link_libraries(dxcompiler vkd3d_shader cpprt uuid)
add_importlibs(dxcompiler ole32 msvcrt kernel32 ntdll)
if(NOT ENABLE_DXCOMPILER_L2)
    add_cd_file(TARGET dxcompiler DESTINATION reactos/system32 FOR all)
endif()
