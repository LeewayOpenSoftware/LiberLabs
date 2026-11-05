#!/bin/sh
# Print the fast-build steps as a plain local command list (no GitHub needed), so the same
# target list can be built on any machine with the pinned RosBE (e.g. a borrowed workstation).
#
#   sh ci/local-commands.sh <libernt-src-dir> "<ninja targets>" [config] [extra -D flags]
#   config: clang-amd64 (default) | gcc-amd64 | clang-i386 | clang-arm64
#
# Output is a script you can review and run; nothing is executed here.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
. "$here/toolchain.env"
src=${1:?libernt source dir}; targets=${2:?ninja targets}; config=${3:-clang-amd64}; extra=${4:-}
compiler=${config%%-*}; arch=${config#*-}
flags=""
[ "$compiler" = clang ] && flags="$flags --clang"
[ "$compiler" = gcc ] && flags="$flags --gcc"
[ "$compiler" = clang ] && [ "$arch" = amd64 ] && flags="$flags -DENABLE_MESA_LLVMPIPE=ON"
envname=$([ "$compiler" = clang ] && echo Clang || echo GCC)
outdir="output-$envname-$arch-debug"
cat <<EOF
#!/bin/sh
# LiberLabs fast-build, local equivalent. Toolchain pin: $ROSBE_BOOTSTRAP_REPO@$ROSBE_BOOTSTRAP_COMMIT
# (sha256 $ROSBE_BOOTSTRAP_SHA256). Config: $config. Targets: $targets
set -eu
# 1. toolchain (once; verified download)   -> sh $here/install-rosbe.sh
. "$ROSBE_PREFIX/rosbe-env.sh"
cd "$src"
# 2. configure (same flags as the LiberNT build.yml on leeway/main)
./configure.sh$flags -a $arch $CMAKE_COMMON_FLAGS $extra
# 3. submodules, then the targets
ninja -C $outdir submodules
ninja -C $outdir -k0 $targets
# 4. host-side export dumps + forwarder-loop check (optional)
python3 $here/export-check.py dump $outdir exports-out $(for t in $targets; do printf '%s.dll ' "$t"; done)
python3 -u sdk/tools/check-forwarder-loops.py $outdir
EOF
# Windows (RosBE for Windows, cmd) equivalent of steps 2-3:
cat <<EOF
# --- Windows / cmd equivalent (RosBE for Windows on PATH) ---
#   cd $src
#   configure.cmd /$compiler /arch=$arch $CMAKE_COMMON_FLAGS $extra
#   ninja -C $outdir submodules
#   ninja -C $outdir -k0 $targets
EOF
