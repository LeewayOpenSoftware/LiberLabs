# PROVENANCE (project LiberX, provisional name; repository https://github.com/LeewayOpenSoftware/LiberX)

## Source
- Repository: `LeewayOpenSoftware/LiberNT-LDW`, branch `leeway/main`, commit `65bfb3bdbab44ec74deca826e18ba82dc6175db4` (the tip after the 184-commit upstream sync). The snapshot was first built on `2eef070f5ded71e0d5364741e21b6a32566012d2` and refreshed to this commit in one change (77 changed and 4 new files under the kept paths; the CMake glue files were kept).
- Method: `git archive` of the paths in `DX-PATHS.txt`, then the CMake glue edits listed in `README-DX.md`. No file content was otherwise modified.
- History: imported with `git filter-repo` from a full clone of `leeway/main` at 65bfb3bdba (12387 commits, original authors and dates), using `DX-PATHS.txt` plus `scripts`, `dll/3rdparty/mbedtls`, `dll/win32/dbghelp` and `dll/appcompat/apphelp`. The vendored `submodules/mesa` and `submodules/fex-arm64ec` trees of older commits were dropped. It is attached by a merge that keeps the snapshot tree.

## Origins and licenses (unchanged from LiberNT-LDW)
| Part | Origin | License (as stated in the files) |
|---|---|---|
| `dll/directx/wine/*` | Wine (sync markers `Synced to Wine-11.14`) via the LiberNT NT10 fork | LGPL-2.1-or-later (per-file headers) |
| `sdk/lib/3rdparty/vkd3d` | vkd3d project (`COPYING`: GNU LGPL) | LGPL |
| ReactOS-native modules, build system, `base/applications/dxdiag`, `sdk/**`, `win32ss/**`, system DLL directories | ReactOS and the LiberNT fork | GPL-2.0-or-later / LGPL as in each file header and `COPYING*` |
| Other third-party code under `sdk/lib/3rdparty` and `dll/win32/*` | their upstream projects | per-file / per-directory notices, kept |

## Rules
Leeway Open Software identity for commits, no AI attribution lines, all license and copyright notices kept, no Microsoft binaries, no upstream contact, private repository only. Source-comment policy of 2026-11-02 applies to new code.
