# LiberLabs

Private continuous integration, remote build and compatibility testing infrastructure for
Leeway Open Software. Builds LiberNT revisions on GitHub-hosted Linux runners so that local
machines spend their resources on development instead of multi-hour builds.

Everything in this repository is manual-dispatch only (`workflow_dispatch`): nothing builds
on push, nothing runs on pull requests, no self-hosted runners, no secrets in build steps, and
the workflow token has `contents: read` (plus `actions: read` where a baseline artifact is
downloaded). Force-pushes are never used.

## Layout

```
.github/workflows/fast-build.yml     targeted ninja targets + host-side export checks (minutes: ~10-25)
.github/workflows/full-build.yml     full build, images, manifest, checksums         (minutes: ~30-45 warm, ~60-100 cold)
.github/workflows/compat-tests.yml   forwarder cycles, NUMA export shape, export diffs (minutes: ~15-30)
ci/toolchain.env                     the pinned toolchain (bootstrap commit + sha256)
ci/install-rosbe.sh                  sha256-verified install of that toolchain
ci/verify-source.sh                  proves the checkout is the submitted revision
ci/export-check.py                   llvm-readobj export dumps, NUMA expectations, diffs
ci/manifest.py                       manifest.json + SHA256SUMS + step summary
ci/submit.sh                         agent-side helper: push a revision, dispatch, watch, download
main                                 CI configuration only
src/<branch>                         submitted LiberNT source revisions (see below)
```

## Source transfer: why `src/<branch>` refs, and why the whole history comes along

LiberNT's public repository must never carry private experimental work, and a workflow in this
repository can only check out commits that are reachable from a ref **of this repository**
(GitHub only serves reachable objects). So a revision is submitted by pushing its exact SHA into
LiberLabs as `refs/heads/src/<branch>`:

```
sh ci/submit.sh push <local-libernt-clone> kb-layering-1 28dd6784…(40 hex)
```

That push necessarily brings the commit's full ancestry, i.e. the LiberNT history, into this
private repository the first time; later pushes are incremental. Copying a source tarball per
build instead would be larger per run, lose the git metadata the build system uses for version
strings, and still have to live somewhere private. One ref per branch, updated fast-forward only
(`git push` without `--force` refuses anything else), keeps every submitted revision reviewable.
The workflow then re-verifies that `HEAD` equals the submitted SHA and that the SHA is the head
of the named ref (`allow_older_revision` relaxes only the second check).

Never push to LiberNT from here. Never merge unrelated branches to make them build together:
each branch is built separately at its exact SHA.

## Agent workflow

1. Code locally, commit to your isolated branch, note the SHA: `git rev-parse HEAD`.
2. Submit the revision: `sh ci/submit.sh push <clone> <branch> <sha>`.
3. Dispatch:
   - `sh ci/submit.sh fast <branch> <sha> "kernel32 kernelbase"` (targets = ninja target names)
   - `sh ci/submit.sh full <branch> <sha> clang-amd64 bootcd 7`
   - `sh ci/submit.sh compat <branch> <sha> [baseline-run-id]`
   (plain `gh workflow run <file> -R LeewayOpenSoftware/LiberLabs -f source_ref=refs/heads/src/<branch> -f source_sha=<sha> …` works too.)
4. Watch: `sh ci/submit.sh watch <run-id>` (`gh run watch --exit-status`). The job summary shows the
   source, the manifest table (status, duration, error/warning counts, toolchain pin, artifact sha256s),
   the first errors, and the check results.
5. Retrieve: `sh ci/submit.sh fetch <run-id> [dir]` (`gh run download`). Artifacts:
   - fast: `fast-<config>-<sha>/` with `bin/` (built targets), `exports/`, `manifest.json`, `SHA256SUMS`, logs
   - full: `…-manifest` (manifest, checksums, logs, export dumps) and `…-images` (ISO etc.)
   - compat: `compat-<config>-<sha>/` with the check logs, export dumps, manifest
6. Fix locally, repeat. Nothing is merged anywhere by CI.

`gh` must be logged in with access to this private repository (`repo`, `workflow` scopes). The
credential stays in the agent's local keyring; it is never stored in this repository or in artifacts.

## Toolchain (pinned)

`ci/toolchain.env` pins the `LiberNT/libernt-build-toolchain` bootstrap script by commit
(`2bdf34fa70f6`) and sha256; `ci/install-rosbe.sh` refuses to run a script that does not match.
That script installs LLVM-MinGW 20261007 (ucrt), LLVM/clang/lld 20261015 and MinGW-GCC 16.2.0
into `~/.local/opt/rosbe` (cached by the pin). Host tools (cmake, ninja, flex, bison, ccache,
python modules) come from the `ubuntu-24.04` runner image. Changing the pin is a reviewed commit
on `main`. The configure flags mirror LiberNT's own `.github/workflows/build.yml` on `leeway/main`
(`-DREACTOS_TARGET_NT=0xA00 -DDIAGDATA_CHANNEL= -DLIBERNT_BUILD_UPDATE_PENDING=ON -DENABLE_CCACHE=1`,
`-DENABLE_MESA_LLVMPIPE=ON` for clang/amd64).

## Supported build targets

Any ninja target of the LiberNT tree at the submitted revision. Component names used so far:

| area | targets |
|---|---|
| kernel32 / kernelbase | `kernel32 kernelbase` (+ `kernel32_apitest`) |
| ntdll | `ntdll` |
| DirectWrite / GDI32 / win32k | `dwrite gdi32 win32k` |
| crypt32 / schannel | `crypt32 schannel secur32` |
| networking | `ws2_32 iphlpapi dnsapi wininet tcpip ndis afd` |
| Network and Sharing Center | the branch's own targets (ask the branch owner; add here once known) |
| LiberX / graphics | LiberX is a separate repository: submit its revision the same way (`src/liberx-<branch>`) once its tree is pushed here, or use its own CI |
| full | `full-build.yml` (everything + images) |

Configurations: `clang-amd64` (default, what Leeway's CI and local builds use), `gcc-amd64`,
`clang-i386`, `clang-arm64` (the last two are offered, not yet validated on the runner).

## Runner specifications and limits (GitHub-hosted, as documented by GitHub, 2026-11)

- `ubuntu-24.04` standard runner: 4 vCPU, 16 GB RAM, 14 GB free SSD before the "free disk space"
  step (the full build removes unused SDKs to gain ~25 GB more). Job limit 6 h; we set 5 h for
  full builds, 2 h for the others.
- A full LiberNT build output is ~5 GB; images add ~0.5-1.5 GB.
- Private repository on a **free user plan**: ~2 000 Actions minutes/month (Linux counts 1x) and
  **500 MB of artifact/package storage**. A `bootcd.iso` is ~450 MB, so one retained image already
  fills most of that quota: keep `retention_days` short (7), upload `bootcd` only by default, and
  delete image artifacts after download. Caches (RosBE ~1-2 GB, ccache 1 GB per config) have a
  separate 10 GB per-repository cache limit and are evicted automatically.
- No GPU, no nested virtualization guarantee: nothing here runs LiberNT. All checks are host-side.

## Actions minutes budget (measured, not guessed)

From the public LiberNT `Build` workflow history (same toolchain, same configure flags):

| run | duration |
|---|---|
| Leeway fork, Linux clang amd64 job, warm ccache (`36a28891ba`, the applier + images) | **29 min** |
| Leeway fork, whole workflow incl. the MSVC job | 50-99 min |
| upstream `main-nt10`, full | 60-115 min |

Estimates for LiberLabs on a 4-vCPU runner: **fast-build 10-25 min** (RosBE cache hit ~2 min,
configure ~2 min, submodules ~3 min, targets), **full-build ~30-45 min with a warm ccache,
~60-100 min cold** (first run per config, or after a cache eviction), **compat-tests 15-30 min**.
On 2 000 min/month that is roughly 40 warm full builds, or e.g. 10 full + 60 fast runs. Spending
beyond the free quota needs the owner's approval; the exact plan of the `LeewayOpenSoftware`
account should be confirmed, since Pro/Team plans raise both the minutes and the artifact storage.

## What CI does not claim

Every result here is a compile/link/export-table fact measured on the runner. LiberNT runtime
behaviour (boot, API behaviour, driver behaviour) is established only on a VM or real hardware.
