#!/usr/bin/env bash
# L19-dxc-l2 stage A (Lab03, Linux): fetch the LiberX branch and the pinned DXC tag into the
# LiberX tree, then write the license/provenance manifest and the host preflight. NO BUILD.
# usage: stageA.sh <liberx tree> [out dir]
set -uo pipefail
LX=${1:?usage: stageA.sh <liberx tree> [out dir]}
OUT=${2:-$PWD/L19-A}
SHA=9b328b9fa158102ba0cc8ef5bf2ac32ac13da578
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
cd "$LX" || exit 2

git fetch https://github.com/LeewayOpenSoftware/LiberLabs.git \
    refs/heads/src/liberx-item18-dxc-l2:refs/heads/liberx-item18-dxc-l2 2>&1 | tail -2 ||
git fetch "$HERE/liberx-item18-dxc-l2.bundle" \
    refs/heads/liberx/item18-dxc-l2:refs/heads/liberx-item18-dxc-l2 2>&1 | tail -2
git checkout -q "$SHA" || { echo "cannot check out $SHA"; exit 2; }
git log -1 --format='%H %an <%ae> %s' | tee "$OUT/source.txt"

# The feed line in feeds.conf.default pins DirectXShaderCompiler v1.10.2605.37 and checks out
# only the two SPIRV submodules (depth 1). Nothing is configured or compiled here.
scripts/feeds update dxc 2>&1 | tee "$OUT/feeds.log"

cd submodules/dxc || { echo "feed checkout missing"; exit 2; }
{
echo "== dxc feed"
echo "HEAD $(git rev-parse HEAD)   expected c4d8f4f99aa221da58cd540bf2099a2889632ab6 (= tag v1.10.2605.37, 2026-08-06)"
git describe --tags --exact-match 2>/dev/null || echo "(no exact tag name in a depth-1 clone; compare the SHA)"
echo "== nested submodules (expected: SPIRV-Headers, SPIRV-Tools checked out; googletest, DirectX-Headers NOT)"
git submodule status
du -sh . external/SPIRV-Headers external/SPIRV-Tools 2>/dev/null
echo "== top-level notices"
sha256sum LICENSE.TXT ThirdPartyNotices.txt
echo "== every license/notice file in the checkout (path : first 100 chars)"
find . -path ./.git -prune -o \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'NOTICE*' -o -iname 'COPYRIGHT*' \) -type f -print | sort |
    while read -r f; do printf '%s : %s\n' "$f" "$(head -c 100 "$f" | tr '\n\r' '  ')"; done
echo "== external/ directories"
ls -la external/
echo "== tracked binaries in the feed (nothing here may be shipped; expected: none or test data only)"
git ls-files | grep -iE '\.(dll|exe|lib|so|a|bin|pdb)$' || echo "none tracked"
echo "== dxil.dll: there must be NO such file; only the optional-loader strings in the source"
find . -iname 'dxil.dll' -print
git grep -n -i '"dxil"\|dxil\.dll' -- lib/DxcSupport tools/clang/tools/dxcompiler | head -20
echo "== validator path: dxcompiler links the open-source validator statically (dxcvalidator, DxilValidation)"
grep -n 'dxcvalidator\|LLVMDxilDia' tools/clang/tools/dxcompiler/CMakeLists.txt
grep -n 'CreateDxcValidator\|RunInternalValidator' tools/clang/tools/dxcompiler/dxcutil.cpp | head
echo "== dxcompiler exports (must be exactly DxcCreateInstance, DxcCreateInstance2)"
cat tools/clang/tools/dxcompiler/DXCompiler.def
echo "== portability patch (submodules/dxc-reactos-sdk.patch) applies cleanly"
git apply --check ../dxc-reactos-sdk.patch && echo PATCH-CHECK-OK
} > "$OUT/MANIFEST-stageA.txt" 2>&1

cd "$LX"
{
echo "== host"; uname -a; echo "nproc=$(nproc)"; free -h; df -h .
echo "== tools"; git --version; cmake --version | head -1; echo "ninja $(ninja --version)"; python3 --version
(cc --version 2>/dev/null | head -1; c++ --version 2>/dev/null | head -1)
MINGW=$(grep -m1 '^REACTOS_CLANG_LLVM_MINGW_ROOT' output-Clang-amd64-debug/CMakeCache.txt 2>/dev/null | cut -d= -f2)
echo "mingw root: ${MINGW:-?}"; "${MINGW:-/nonexistent}/bin/clang" --version 2>/dev/null | head -1; ls "${MINGW:-/nonexistent}/bin/llvm-strip" 2>/dev/null
echo "== LiberX"; echo "HEAD $(git rev-parse HEAD)"
echo "integration files last changed in $(git log -1 --format=%H -- submodules/dxilconv.cmake submodules/dxc-reactos-sdk.patch feeds.conf.default)"
git diff --check && echo "diff --check clean"
echo "== not built"; ls output-Clang-amd64-debug/submodules/dxilconv 2>/dev/null || echo "no nested build dir (expected before stage B)"
} > "$OUT/PREFLIGHT-stageA.txt" 2>&1

echo "stage A done -> $OUT ($(wc -l < "$OUT/MANIFEST-stageA.txt") manifest lines)"
