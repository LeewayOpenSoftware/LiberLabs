# LiberX (provisional name)

LiberX is a private Leeway Open Software research repository for the DirectX user-mode components of LiberNT: research, completion and parity work. Finished work is meant to be brought back into LiberNT once it roughly works.

Origin: derived from LiberNT-LDW `leeway/main` at `2eef070f5ded71e0d5364741e21b6a32566012d2` (stripped to DirectX plus what builds it; history not imported yet).

## What builds
The Wine-derived Direct3D 8/9/10/11/12, DXGI, Direct2D, DirectDraw, DirectSound, DirectInput, DirectWrite and dxdiagn modules, `xinput1_4`, and the vkd3d library: 19 DLLs link on clang amd64 (verified locally on 2026-11-03). Not built: `vulkan-1` (needs the mesa feed), the DirectX test suites (not in the snapshot).

## How to build
```
./configure.sh --clang -a amd64 -DREACTOS_TARGET_NT=0xA00 -DDIAGDATA_CHANNEL= -DENABLE_WOW64=OFF -DENABLE_ROSTESTS=OFF -DENABLE_ROSAPPS=OFF
ninja -C output-Clang-amd64-debug d3d12 d3d11 dxgi d3d9 d3d8 ddraw dsound dinput dinput8 dwrite d2d1 xinput1_4 d3d10 d3d10core d3d10_1 d3dcompiler_47 dcomp dxdiagn dxcore vkd3d
```
The toolchain is RosBE (see the CI workflow in `.github/workflows/build.yml` for the exact steps).

## More
- [README-DX.md](README-DX.md): layout, the CMake changes against LiberNT-LDW, known limits.
- [PROVENANCE.md](PROVENANCE.md): source commit, origins and licenses.
- [README-LiberNT.md](README-LiberNT.md): the README inherited from LiberNT, kept unchanged for reference.

License and copyright notices (`COPYING*`, `CREDITS.md`, per-file headers) are kept as inherited.
