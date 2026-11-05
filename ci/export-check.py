#!/usr/bin/env python3
"""Host-side export checks on built PE DLLs (llvm-readobj --coff-exports). No binary is executed.

  export-check.py dump  <build-dir> <out-dir> [dll ...]   write <out-dir>/<dll>.exports.txt (sorted "name -> target|body")
  export-check.py numa  <build-dir>                         NUMA export-shape expectations (kernel32/kernelbase)
  export-check.py diff  <baseline-dir> <current-dir>        diff the dumps of two runs, summary to stdout, exit 0 always

These are static ABI/export-table facts. They say nothing about LiberNT runtime behaviour.
"""
import os, re, subprocess, sys

DEFAULT_DLLS = ["kernel32.dll", "kernelbase.dll", "ntdll.dll", "gdi32.dll", "dwrite.dll", "crypt32.dll",
                "schannel.dll", "secur32.dll", "ws2_32.dll", "iphlpapi.dll", "dnsapi.dll", "wininet.dll"]

def find_dll(build_dir, name):
    for root, _dirs, files in os.walk(build_dir):
        if name in files and not root.endswith("_wow64_i386"):
            return os.path.join(root, name)
    return None

def exports(path):
    txt = subprocess.check_output(["llvm-readobj", "--coff-exports", path]).decode("utf-8", "replace")
    res = {}
    for block in txt.split("Export {")[1:]:
        name = re.search(r"Name:\s*(\S+)", block)
        fwd = re.search(r"ForwardedTo:\s*(\S+)", block)
        if name:
            res[name.group(1)] = fwd.group(1) if fwd else None
    return res

def cmd_dump(build_dir, out_dir, dlls):
    os.makedirs(out_dir, exist_ok=True)
    missing = []
    for dll in dlls or DEFAULT_DLLS:
        p = find_dll(build_dir, dll)
        if not p:
            missing.append(dll); continue
        ex = exports(p)
        with open(os.path.join(out_dir, dll + ".exports.txt"), "w") as f:
            for n in sorted(ex):
                f.write("%s -> %s\n" % (n, ex[n] or "body"))
        print("%s: %d exports (%d forwarders)" % (dll, len(ex), sum(1 for v in ex.values() if v)))
    if missing:
        print("not built in this run (skipped): " + " ".join(missing))
    return 0

def cmd_numa(build_dir):
    k32p, kbp = find_dll(build_dir, "kernel32.dll"), find_dll(build_dir, "kernelbase.dll")
    if not k32p or not kbp:
        print("::error::kernel32.dll or kernelbase.dll not found under " + build_dir); return 2
    k32, kb = exports(k32p), exports(kbp)
    fails = 0
    def check(cond, msg):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + msg)
        if not cond:
            fails += 1
    # Leeway layering: kernelbase implements, kernel32 forwards (owner NUMA decision 2026-11-04)
    for n in ["CreateFileMappingNumaA", "CreateFileMappingNumaW", "MapViewOfFileExNuma", "VirtualAllocExNuma"]:
        check(k32.get(n) == "kernelbase." + n, "kernel32!%s -> %s (want kernelbase.%s)" % (n, k32.get(n, "<absent>"), n))
        check(n in kb and kb[n] is None, "kernelbase!%s has the body" % n)
    # kernel32 implements these; kernelbase, where it exports them, forwards back
    for n in ["AllocateUserPhysicalPagesNuma", "GetNumaAvailableMemoryNodeEx", "GetNumaNodeNumberFromHandle",
              "GetNumaNodeProcessorMask2", "GetNumaProximityNode", "GetNumaProximityNodeEx", "GetNumaHighestNodeNumber"]:
        check(n in k32 and k32[n] is None, "kernel32!%s has the body" % n)
        if n in kb:
            check(kb[n] == "kernel32." + n, "kernelbase!%s -> %s (want kernel32.%s)" % (n, kb[n], n))
    print("NUMA-EXPORT-CHECK %s (fails=%d) [host-side export-table check, not a runtime result]" % ("PASS" if not fails else "FAIL", fails))
    return 1 if fails else 0

def cmd_diff(base, cur):
    names = sorted(set(os.listdir(base)) | set(os.listdir(cur)))
    for n in names:
        if not n.endswith(".exports.txt"):
            continue
        b = set(open(os.path.join(base, n)).read().splitlines()) if os.path.exists(os.path.join(base, n)) else None
        c = set(open(os.path.join(cur, n)).read().splitlines()) if os.path.exists(os.path.join(cur, n)) else None
        if b is None or c is None:
            print("%s: only in %s" % (n, "current" if b is None else "baseline")); continue
        added, removed = sorted(c - b), sorted(b - c)
        print("%s: +%d -%d" % (n, len(added), len(removed)))
        for l in added[:50]:
            print("  + " + l)
        for l in removed[:50]:
            print("  - " + l)
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__); sys.exit(2)
    if a[0] == "dump":
        sys.exit(cmd_dump(a[1], a[2], a[3:]))
    if a[0] == "numa":
        sys.exit(cmd_numa(a[1]))
    if a[0] == "diff":
        sys.exit(cmd_diff(a[1], a[2]))
    print(__doc__); sys.exit(2)
