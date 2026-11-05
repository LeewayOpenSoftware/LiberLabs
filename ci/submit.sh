#!/bin/sh
# Agent-side source transfer + dispatch for LiberLabs.
#
#   submit.sh push     <local-libernt-clone> <branch-name> <sha>        push <sha> as refs/heads/src/<branch-name>
#   submit.sh fast     <branch-name> <sha> "<ninja targets>" [config]   dispatch fast-build.yml
#   submit.sh full     <branch-name> <sha> [config] [images] [retention] dispatch full-build.yml
#   submit.sh compat   <branch-name> <sha> [baseline-run-id] [config]    dispatch compat-tests.yml
#   submit.sh watch    <run-id>                                           gh run watch + exit status
#   submit.sh fetch    <run-id> [dir]                                     gh run download (all artifacts)
#
# Rules enforced here: an explicit full SHA, fast-forward pushes only (no --force), one ref per
# branch under src/, nothing is ever pushed to the public LiberNT repository.
set -eu
REPO=${LIBERLABS_REPO:-LeewayOpenSoftware/LiberLabs}
cmd=${1:-}; shift || true

full_sha() {
    case "$1" in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
        *) echo "need a full 40-hex sha, got '$1' (use: git rev-parse <ref>)"; exit 1 ;;
    esac
}

case "$cmd" in
push)
    clone=$1; branch=$2; sha=$3; full_sha "$sha"
    git -C "$clone" cat-file -e "$sha^{commit}" || { echo "$sha is not a commit in $clone"; exit 1; }
    # fast-forward only: git refuses a non-ff update of refs/heads/src/<branch> without --force, which we never pass
    git -C "$clone" push "https://github.com/$REPO.git" "$sha:refs/heads/src/$branch"
    echo "pushed $sha -> $REPO refs/heads/src/$branch"
    ;;
fast)
    branch=$1; sha=$2; targets=$3; config=${4:-clang-amd64}; full_sha "$sha"
    gh workflow run fast-build.yml -R "$REPO" -f source_ref="refs/heads/src/$branch" -f source_sha="$sha" -f targets="$targets" -f config="$config"
    sleep 5; gh run list -R "$REPO" --workflow fast-build.yml --limit 1 --json databaseId,status,url --jq '.[0] | "run " + (.databaseId|tostring) + " " + .status + " " + .url'
    ;;
full)
    branch=$1; sha=$2; config=${3:-clang-amd64}; images=${4:-bootcd}; retention=${5:-7}; full_sha "$sha"
    gh workflow run full-build.yml -R "$REPO" -f source_ref="refs/heads/src/$branch" -f source_sha="$sha" -f config="$config" -f images="$images" -f retention_days="$retention"
    sleep 5; gh run list -R "$REPO" --workflow full-build.yml --limit 1 --json databaseId,status,url --jq '.[0] | "run " + (.databaseId|tostring) + " " + .status + " " + .url'
    ;;
compat)
    branch=$1; sha=$2; baseline=${3:-}; config=${4:-clang-amd64}; full_sha "$sha"
    gh workflow run compat-tests.yml -R "$REPO" -f source_ref="refs/heads/src/$branch" -f source_sha="$sha" -f baseline_run_id="$baseline" -f config="$config"
    sleep 5; gh run list -R "$REPO" --workflow compat-tests.yml --limit 1 --json databaseId,status,url --jq '.[0] | "run " + (.databaseId|tostring) + " " + .status + " " + .url'
    ;;
watch)
    gh run watch "$1" -R "$REPO" --exit-status
    ;;
fetch)
    dir=${2:-liberlabs-run-$1}; mkdir -p "$dir"
    gh run download "$1" -R "$REPO" -D "$dir"; echo "artifacts in $dir"; ls -1 "$dir"
    ;;
*)
    sed -n '2,14p' "$0"; exit 2 ;;
esac
