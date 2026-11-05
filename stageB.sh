#!/usr/bin/env bash
# L19-dxc-l2 stage B (Lab03, Linux, in the ~21:00Z window): configure + build the Level 2
# dxcompiler.dll with caps, timing and resource logging. Then the LiberX test tools and the
# Level 1 control, and dxc.exe best-effort (not gated).
# usage: stageB.sh <liberx tree> [jobs=2] [out dir]
set -uo pipefail
LX=${1:?usage: stageB.sh <liberx tree> [jobs=2] [out dir]}
J=${2:-2}
OUT=${3:-$PWD/L19-B}
B=output-Clang-amd64-debug
mkdir -p "$OUT"
cd "$LX" || exit 2
[ "$(git rev-parse HEAD)" = 9b328b9fa158102ba0cc8ef5bf2ac32ac13da578 ] || { echo "wrong LiberX SHA $(git rev-parse HEAD)"; exit 2; }
[ -f submodules/dxc/CMakeLists.txt ] || { echo "run stage A first (submodules/dxc missing)"; exit 2; }

date -u +%FT%TZ | tee "$OUT/start.txt"
df -B1 --output=avail . | tail -1 > "$OUT/disk-avail-before.txt"

# Caps: nice 10; RAM 8 GB as a cgroup when systemd-run works, else a per-process ulimit -v;
# -j$J for the outer ninja (build-with-parallel.py forwards it to the nested LLVM build);
# the nested link runs one job at a time (LLVM_PARALLEL_LINK_JOBS=1 in dxilconv.cmake).
CAP=(nice -n 10)
if systemd-run --user --scope -p MemoryMax=8G --quiet true 2>/dev/null; then
    CAP=(systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0 --quiet nice -n 10)
else
    ulimit -v 8388608
fi
echo "caps: ${CAP[*]} -j$J" | tee "$OUT/caps.txt"

"${CAP[@]}" cmake -DENABLE_DXILCONV=ON -DENABLE_DXCOMPILER_L2=ON "$B" > "$OUT/configure.log" 2>&1
echo "configure rc=$?" | tee -a "$OUT/configure.log"
grep -E '^ENABLE_DXILCONV|^ENABLE_DXCOMPILER_L2' "$B/CMakeCache.txt" | tee -a "$OUT/configure.log"

# Milestones for the lab report: configure done; nested build 25/50/75 % (the [n/N] lines that
# build-with-parallel.py mirrors to the terminal); link of dxcompiler.dll; strip/copy.
/usr/bin/time -v "${CAP[@]}" ninja -C "$B" -j"$J" -k 0 dxcompiler_l2 > "$OUT/build.log" 2> "$OUT/time.txt"
RC=$?
echo "build rc=$RC" | tee -a "$OUT/build.log"
date -u +%FT%TZ | tee "$OUT/end.txt"
grep -E 'Elapsed|Maximum resident|User time|System time' "$OUT/time.txt" | tee "$OUT/resources.txt"
df -B1 --output=avail . | tail -1 > "$OUT/disk-avail-after.txt"
du -sh "$B/submodules/dxilconv" "$B/submodules/dxilconv/host-build" "$B/submodules/dxilconv/build" 2>/dev/null | tee -a "$OUT/resources.txt"
echo "warnings=$(grep -ciE 'warning:' "$OUT/build.log") errors=$(grep -ciE 'error:' "$OUT/build.log")" | tee -a "$OUT/resources.txt"

W="$B/submodules/dxilconv"
READOBJ=$(command -v llvm-readobj || ls "$(grep -m1 '^REACTOS_CLANG_LLVM_MINGW_ROOT' "$B/CMakeCache.txt" | cut -d= -f2)/bin/llvm-readobj" 2>/dev/null)
{
echo "== products"
ls -l "$W/dxilconv.dll" "$W/dxcompiler.dll" "$W/DirectXShaderCompiler-LICENSE.TXT" "$W/build/bin/dxcompiler.dll" 2>&1
sha256sum "$W/dxilconv.dll" "$W/dxcompiler.dll" "$W/DirectXShaderCompiler-LICENSE.TXT" 2>&1
echo "== exports of dxcompiler.dll (must be DxcCreateInstance and DxcCreateInstance2)"
"${READOBJ:-llvm-readobj}" --coff-exports "$W/dxcompiler.dll" 2>&1 | grep -E 'Name:'
echo "== imports of dxcompiler.dll (system DLLs only; dxil.dll must NOT appear)"
"${READOBJ:-llvm-readobj}" --coff-imports "$W/dxcompiler.dll" 2>&1 | grep -E '^\s*Name:' | sort -u
} > "$OUT/products.txt" 2>&1
cat "$OUT/products.txt"

# LiberX side: the two test tools and the Level 1 dxcompiler (control). Gated: rc must be 0.
"${CAP[@]}" ninja -C "$B" -j"$J" -k 0 liberx_dxc liberx_dxc_sm6 dxcompiler > "$OUT/tests-build.log" 2>&1
echo "tests build rc=$?" | tee -a "$OUT/tests-build.log"
sha256sum "$B/tests/liberx-dxc/liberx_dxc.exe" "$B/tests/liberx-dxc/liberx_dxc_sm6.exe" "$B/dll/directx/dxc/dxcompiler/dxcompiler.dll" 2>&1 | tee -a "$OUT/tests-build.log"

# Best effort, not gated: the upstream dxc.exe CLI from the same nested build.
"${CAP[@]}" ninja -C "$W/build" -j"$J" dxc > "$OUT/dxc-exe.log" 2>&1
echo "dxc.exe rc=$? (best effort, not gated)" | tee -a "$OUT/dxc-exe.log"
ls -l "$W/build/bin/dxc.exe" 2>/dev/null | tee -a "$OUT/dxc-exe.log"

echo "stage B done rc=$RC -> $OUT"
exit $RC
