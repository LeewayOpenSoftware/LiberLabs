# LiberX (provisional name)

LiberX is a private research repository (https://github.com/LeewayOpenSoftware/LiberX) for the DirectX user-mode components of LiberNT: research, completion and parity work, to be synced back into LiberNT-LDW once it roughly works. Owner: Leeway Open Software. Not for redistribution.

## What this is
A **flat snapshot** of the DirectX-relevant part of LiberNT-LDW `leeway/main` at `2eef070f5d` (the post-upstream-sync tip was not available yet), stripped to DirectX plus the minimum needed to build it. History is **not** included yet: it will be imported with `git filter-repo` on the same path list (`DX-PATHS.txt`) when disk space allows. See `PROVENANCE.md`.

## Layout
- `dll/directx/**`: Wine-derived Direct3D 8/9/10/11/12, DXGI, Direct2D, DirectDraw, DirectSound, DirectInput, DirectMusic, wined3d, vkd3d-based D3D12, dxdiagn and the ReactOS-native (disabled) modules.
- `dll/win32/{dwrite,dxcore,dxva2,evr,mfplat,vulkan-1,xinput*,...}` and the system DLL directories needed only for their import libraries (`kernel32`, `advapi32`, `ole32`, `winmm`, `comctl32`, ...).
- `sdk/**`, `tools/`, `submodules/*.cmake`, root `CMakeLists.txt`, `win32ss/` (import-library and header providers, `win32ss/reactx` kept as source).
- `base/applications/dxdiag`.
- Not in this snapshot: kernel, HAL, drivers, boot, other applications, the `modules/rostests` DirectX tests (they arrive with the history import).

## Build (clang, amd64)
```
./configure.sh --clang -a amd64 -DREACTOS_TARGET_NT=0xA00 -DDIAGDATA_CHANNEL= -DENABLE_WOW64=OFF -DENABLE_ROSTESTS=OFF -DENABLE_ROSAPPS=OFF
ninja -C output-Clang-amd64-debug d3d12 d3d11 dxgi d3d9 d3d8 ddraw dsound dinput dinput8 dwrite d2d1 xinput1_4 d3d10 d3d10core d3d10_1 d3dcompiler_47 dcomp dxdiagn dxcore vkd3d
```
Verified on 2026-11-03 (local, Windows host, RosBE LLVM, `ninja -j1`): configure with 0 errors and the 19 DX DLLs link (`d3d12 d3d11 dxgi d3d9 d3d8 ddraw dsound dinput dinput8 dwrite d2d1 d3d10 d3d10_1 d3d10core d3dcompiler_47 dcomp dxcore dxdiagn xinput1_4` plus the vkd3d library). That local build used a pre-built LLVM runtime (`amd64-cxx-runtime`) taken from an older LiberNT build; CI must produce it the way LiberNT-LDW's RosBE bootstrap does.

## Known limits (not built or not verified)
- `vulkan-1` needs the mesa feed (`submodules/mesa`, not checked out), so the Vulkan loader and lavapipe are not part of this build.
- The DX tests (apitests, winetests) are not in the snapshot.
- Import libraries come from the full system DLL directories listed in `dll/win32/CMakeLists.txt`; whether fewer are enough has not been minimized.
- The CI workflow has not run yet when this file is written; see the Actions tab for the result.
- Run-time behaviour on a LiberNT image: see the device-creation diagnostic in the hive (`directx-research/device-test`), not in this repo.

## Changes against LiberNT-LDW (the only edits; marked `DX-PRUNED` in CMake)
Top-level `CMakeLists.txt`: `boot`, `bootloader`, `base`, `submodules`, `drivers`, `hal`, `media`, `modules`, `ntoskrnl`, `subsystems` are not added; four stub custom targets (`bootcd`, `livecd_files`, `preinstall_partition`, `registry_inf`) stand in for targets those directories define; `create_registry_hives()` and `create_iso_lists()` are off. `dll/CMakeLists.txt` and `dll/win32/CMakeLists.txt` add only the kept directories (the original `dll/win32/CMakeLists.txt` is kept as `CMakeLists.txt.orig`). `sdk/lib/CMakeLists.txt`: `fslib` off. `configure.sh`: the glmark2 submodule sync is skipped (not part of DirectX).

## License
See `COPYING*`, `CREDITS.md` and the per-file headers: GPL-2.0-or-later (ReactOS) with LGPL-2.1+ Wine- and vkd3d-derived code. All notices are kept unchanged.
