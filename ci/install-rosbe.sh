#!/bin/sh
# Install the pinned RosBE toolchain on a GitHub-hosted Linux runner.
# Downloads rosbe-unix-bootstrap.sh at the pinned commit, verifies its sha256, runs it.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
. "$here/toolchain.env"

if [ -x "$ROSBE_PREFIX/rosbe-env.sh" ] || [ -f "$ROSBE_PREFIX/rosbe-env.sh" ]; then
    echo "RosBE already present at $ROSBE_PREFIX (cache hit)"
    exit 0
fi

url="https://raw.githubusercontent.com/$ROSBE_BOOTSTRAP_REPO/$ROSBE_BOOTSTRAP_COMMIT/rosbe-unix-bootstrap.sh"
tmp=$(mktemp)
echo "Fetching $url"
curl -fsSL "$url" -o "$tmp"
actual=$(sha256sum "$tmp" | awk '{print $1}')
if [ "$actual" != "$ROSBE_BOOTSTRAP_SHA256" ]; then
    echo "::error::rosbe-unix-bootstrap.sh sha256 mismatch: got $actual want $ROSBE_BOOTSTRAP_SHA256"
    exit 1
fi
echo "Bootstrap script verified (sha256 $actual)"
sh "$tmp"
rm -f "$tmp"
test -f "$ROSBE_PREFIX/rosbe-env.sh" || { echo "::error::rosbe-env.sh missing after install"; exit 1; }
echo "RosBE installed at $ROSBE_PREFIX"
