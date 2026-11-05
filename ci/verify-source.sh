#!/bin/sh
# Verify that the checked-out source is exactly the submitted revision.
# usage: verify-source.sh <src-dir> <expected-sha> <source-ref> [allow-older]
#   - HEAD must equal <expected-sha> (40 hex).
#   - <source-ref> (refs/heads/src/<branch>) must exist in this repository.
#   - Unless allow-older=true, <expected-sha> must be the current head of <source-ref>:
#     an explicit, reviewable revision, not "whatever the branch was".
# The checkout itself already proves the SHA lives in this private repository: GitHub
# only serves commits reachable from one of its refs, so nothing outside LiberLabs can be built.
set -eu
src=$1; want=$2; ref=$3; allow_older=${4:-false}
case "$want" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
    *) echo "::error::source_sha must be a full 40-hex commit id, got '$want'"; exit 1 ;;
esac
head=$(git -C "$src" rev-parse HEAD)
if [ "$head" != "$want" ]; then
    echo "::error::checked-out HEAD $head != submitted source_sha $want"; exit 1
fi
case "$ref" in refs/heads/src/*) ;; *) echo "::error::source_ref must be refs/heads/src/<branch>, got '$ref'"; exit 1 ;; esac
refhead=$(git -C "$src" ls-remote --heads origin "${ref#refs/heads/}" | awk '{print $1}')
if [ -z "$refhead" ]; then
    echo "::error::$ref does not exist in this repository"; exit 1
fi
if [ "$refhead" != "$want" ] && [ "$allow_older" != "true" ]; then
    echo "::error::$ref is at $refhead, not at the submitted $want (set allow_older_revision to build an older commit of the branch)"; exit 1
fi
subject=$(git -C "$src" log -1 --format=%s)
echo "Source verified: $want ($ref) - $subject"
{
    echo "### Source"
    echo "- ref: \`$ref\`"
    echo "- sha: \`$want\`"
    echo "- subject: $subject"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
