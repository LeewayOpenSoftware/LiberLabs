# LiberLabs

Private exchange repository of Leeway Open Software: our internal LiberNT branches travel here as
exact-revision refs so that other machines of ours (for example Lab01) can fetch them without
anything going to a public repository.

## MSVC + Rust CI for LiberNT-LDW (`msvc-build`)

Since the repositories became public, GitHub-hosted runners are free, so this repo runs the MSVC
builds that the Linux-only LiberNT-LDW CI skips: the full MSVC build (amd64 and i386, LRM-RS Rust
legs and C-only legs, optional release), `bootcd.iso`, and the MSVC build of `modules/rostests`.
It builds LiberNT-LDW directly (public checkout); no self-hosted runner, no secrets.

How to trigger it (Actions tab -> `msvc-build` -> Run workflow, or from a shell):

```
gh workflow run msvc-build.yml -R LeewayOpenSoftware/LiberLabs -f ref=leeway/main
gh workflow run msvc-build.yml -R LeewayOpenSoftware/LiberLabs -f ref=<branch-or-40-hex-sha> -f legs=amd64-only -f images=false -f rostests=false
```

| input | meaning |
|---|---|
| `repo` | `LiberNT-LDW` (default), `LiberNT-CI` or `LiberNT-sec` |
| `ref` | branch of that repo; LiberNT-LDW branch, tag or full sha (default `leeway/main`) |
| `legs` | `standard` (amd64 lrmrs+conly, i386 lrmrs+conly), `amd64-only`, `with-release` |
| `images` | also build and upload `bootcd.iso` |
| `rostests` | also build `modules/rostests` with MSVC (kmtests/apitests artifacts) |
| `retention_days` | artifact retention (default 3) |

Each leg is one job; failures do not cancel the others, and the Rust toolchain pin comes from
`sdk/cmake/rust.cmake` of the revision being built. The older Linux `fast-build`, `full-build` and
`compat-tests` workflows are the pinned recipe from the private days and stay unused.

## What lives here

```
refs/heads/main                 this README, the unused CI recipe
refs/heads/src/<branch>         one ref per internal LiberNT or LiberX branch, at an explicit revision
```

| ref | revision | what |
|---|---|---|
| `src/upstream-sync-1105` | `5a371715618cf3a888277e8896faa0cdd1ebb783` | leeway/upstream-sync-1105 tip (242-commit upstream sync + owner NUMA adaptation) |
| `src/upstream-fonts-1105` | `60cd1c7d477b8c699186a70a0c2961c00c4c9d44` | Meredith, fonts (dwrite/gdi32/win32k), 8 commits on sync-1105 |
| `src/kb-layering-1` | `28dd6784380d6311f7f11daca303d6d79df54628` | Stanley, kernel32/kernelbase layering |
| `src/crypt32-authroot` | `48e2f2c5d7831cfb89860f912906136fa1f20f70` | Stanley, crypt32 V2 + AuthRoot (contains e812612c, 97bd20bf) |
| `src/ncsc-1` | `6d053c0ac78d90e88bd8232b5b6d0c480a9b55b8` | Pam, Network and Sharing Center |
| `src/net-m1-stats` | `1b5e6b8dd053df830740517c7186525eae262f6b` | Ryan, networking M1 stats |
| `src/liberx-merge-3` | `a98b7124f887e87ee3b558def419e61ba62a6583` | LiberX (separate history, private): merge-3 integrated tree (items 1, 7, 5, 6b, 6, 8, contract-findings) on LiberX main 2627bc87c, + 3 fixes after Lab03 run L6 (d3d12 NULL-out entry, two fixture corrections) |
| `src/liberx-item18-dxc` | `d01990b9d364d8cd5b1b5295f48e75942f2fb793` | LiberX item 18 DXC Level 1 (dxcompiler + liberx_dxc), 4 commits on 2627bc87c |
| `src/liberx-item18-dxc-l2` | `9b328b9fa158102ba0cc8ef5bf2ac32ac13da578` | DXC Level 2: L1 rebased on main e15f0fbec + ENABLE_DXCOMPILER_L2 gate + SM6 fixtures (liberx/item18-dxc-l2); job L19-dxc-l2 |

(The table is maintained by hand when a ref is pushed; `git ls-remote --heads` is authoritative.)

## Exchange procedure

Push an exact revision, fast-forward only, one ref per branch; never `--force`, never a merge of
unrelated branches:

```
git push https://github.com/LeewayOpenSoftware/LiberLabs.git <40-hex sha>:refs/heads/src/<branch>
```

(`sh ci/submit.sh push <clone> <branch> <sha>` does the same with the checks.) The first push of a
branch whose history is not yet here carries its ancestry — the LiberNT history came with
`src/upstream-sync-1105`; later refs are small. Fetch on the other side:

```
git fetch https://github.com/LeewayOpenSoftware/LiberLabs.git refs/heads/src/<branch>:refs/heads/<branch>
```

Pushes come from the agent's own `gh`/git credential on the local machine; nothing is stored here.

## Rules

- Private: never push these refs, or anything derived from them, to a public repository.
- Nothing is merged or pushed to `LiberNT-LDW` from here; integration into `leeway/main` is a
  separate, owner-approved batch coordinated by god.
- Keep branches clean: one logical change per commit, Leeway identity, no force-push.
- No credentials, Microsoft binaries or machine data in the repository.

## Reference: the unused CI recipe

`ci/toolchain.env` pins `LiberNT/libernt-build-toolchain` `rosbe-unix-bootstrap.sh` at commit
`2bdf34fa70f6` (sha256-verified by `ci/install-rosbe.sh`): LLVM-MinGW 20261007, LLVM 20261015,
MinGW-GCC 16.2.0; configure flags as LiberNT's `.github/workflows/build.yml`. `sh ci/local-commands.sh
<src> "<targets>" [config]` prints the equivalent configure + ninja steps as a plain command list for a
workstation build. Measured on the public LiberNT CI: a full Linux clang/amd64 build takes 29 min with
a warm ccache, 60-115 min cold. These workflows were validated by lint only and never ran.
