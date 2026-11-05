# LiberLabs

Private exchange repository of Leeway Open Software: our internal LiberNT branches travel here as
exact-revision refs so that other machines of ours (for example Lab01) can fetch them without
anything going to a public repository.

**No compiling happens here.** By owner decision (2026-11-05, `OWNER-BUILD-ROUTING-2026-11-05.md`,
revision): component and partial builds run on a workstation, and tested code goes to
`LeewayOpenSoftware/LiberNT-LDW`, which builds with its normal CI. The three GitHub Actions
workflows in `.github/workflows/` (`fast-build`, `full-build`, `compat-tests`) are **disabled in
the repository settings and must not be dispatched or re-enabled**; the files are kept as a
reviewed, pinned recipe only. The `ci/` scripts are likewise reference material.

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
