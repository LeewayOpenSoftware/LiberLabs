# PROVENANCE

## Source
- Repository: `LeewayOpenSoftware/LiberNT-LDW`, branch `leeway/main`, commit `2eef070f5ded71e0d5364741e21b6a32566012d2` (2026-11-02; the tip right after the 184-commit upstream sync was not available when this snapshot was made).
- Method: `git archive` of the paths in `DX-PATHS.txt`, then the CMake glue edits listed in `README-DX.md`. No file content was otherwise modified.
- History: not included in this snapshot. It will be imported later with `git filter-repo` using `DX-PATHS.txt`, which keeps the authorship and dates of every commit touching these paths.

## Origins and licenses (unchanged from LiberNT-LDW)
| Part | Origin | License (as stated in the files) |
|---|---|---|
| `dll/directx/wine/*` | Wine (sync markers `Synced to Wine-11.14`) via the LiberNT NT10 fork | LGPL-2.1-or-later (per-file headers) |
| `sdk/lib/3rdparty/vkd3d` | vkd3d project (`COPYING`: GNU LGPL) | LGPL |
| ReactOS-native modules, build system, `base/applications/dxdiag`, `sdk/**`, `win32ss/**`, system DLL directories | ReactOS and the LiberNT fork | GPL-2.0-or-later / LGPL as in each file header and `COPYING*` |
| Other third-party code under `sdk/lib/3rdparty` and `dll/win32/*` | their upstream projects | per-file / per-directory notices, kept |

## Rules
Leeway Open Software identity for commits, no AI attribution lines, all license and copyright notices kept, no Microsoft binaries, no upstream contact, private repository only. Source-comment policy of 2026-11-02 applies to new code.
