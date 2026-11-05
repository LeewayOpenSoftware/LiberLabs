#!/usr/bin/env python3
"""Write the run manifest (JSON + step summary) and sha256 checksums for the artifacts of a run.

usage: manifest.py --out <dir> --source-sha S --source-ref R --config C --status S --started EPOCH
                   [--log build.log] [--artifact PATH ...] [--targets "a b c"] [--toolchain-env ci/toolchain.env]
Counts "error:" / "warning:" lines in the build log, records toolchain pins and duration, and
writes <out>/manifest.json, <out>/SHA256SUMS. Nothing here is a runtime claim.
"""
import argparse, hashlib, json, os, re, time

ap = argparse.ArgumentParser()
ap.add_argument("--out", required=True)
ap.add_argument("--source-sha", required=True)
ap.add_argument("--source-ref", required=True)
ap.add_argument("--config", required=True)
ap.add_argument("--status", required=True)
ap.add_argument("--started", type=int, required=True)
ap.add_argument("--log")
ap.add_argument("--targets", default="")
ap.add_argument("--artifact", action="append", default=[])
ap.add_argument("--toolchain-env", default=os.path.join(os.path.dirname(__file__), "toolchain.env"))
a = ap.parse_args()

os.makedirs(a.out, exist_ok=True)
pins = {}
for line in open(a.toolchain_env):
    line = line.strip()
    if line and not line.startswith("#") and "=" in line:
        k, v = line.split("=", 1)
        pins[k] = v.strip('"')

errors = warnings = 0
first_errors = []
if a.log and os.path.exists(a.log):
    for line in open(a.log, errors="replace"):
        if re.search(r"\berror:", line):
            errors += 1
            if len(first_errors) < 20:
                first_errors.append(line.rstrip()[:300])
        elif re.search(r"\bwarning:", line):
            warnings += 1

sums = []
for p in a.artifact:
    if os.path.isfile(p):
        h = hashlib.sha256()
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        sums.append((h.hexdigest(), os.path.basename(p), os.path.getsize(p)))

duration = int(time.time()) - a.started
m = {
    "source_sha": a.source_sha, "source_ref": a.source_ref, "config": a.config, "targets": a.targets.split(),
    "status": a.status, "duration_seconds": duration, "errors": errors, "warnings": warnings,
    "first_errors": first_errors, "toolchain": pins,
    "runner": {"image": pins.get("RUNNER_IMAGE", "ubuntu-24.04"), "github_run_id": os.environ.get("GITHUB_RUN_ID"),
               "github_run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"), "workflow": os.environ.get("GITHUB_WORKFLOW")},
    "artifacts": [{"sha256": s, "name": n, "bytes": b} for s, n, b in sums],
    "note": "host-side build facts only; not a LiberNT runtime compatibility claim",
}
with open(os.path.join(a.out, "manifest.json"), "w") as f:
    json.dump(m, f, indent=2)
with open(os.path.join(a.out, "SHA256SUMS"), "w") as f:
    for s, n, _b in sums:
        f.write("%s  %s\n" % (s, n))

summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a") as f:
        f.write("### Build manifest\n")
        f.write("| field | value |\n|---|---|\n")
        for k in ("source_sha", "source_ref", "config", "status", "duration_seconds", "errors", "warnings"):
            f.write("| %s | `%s` |\n" % (k, m[k]))
        f.write("| toolchain | bootstrap `%s` (sha256 `%s...`) |\n" % (pins.get("ROSBE_BOOTSTRAP_COMMIT", "?")[:12], pins.get("ROSBE_BOOTSTRAP_SHA256", "?")[:12]))
        if sums:
            f.write("\n| artifact | bytes | sha256 |\n|---|---|---|\n")
            for s, n, b in sums:
                f.write("| %s | %d | `%s` |\n" % (n, b, s))
        if first_errors:
            f.write("\n<details><summary>first errors</summary>\n\n```\n" + "\n".join(first_errors) + "\n```\n</details>\n")
print(json.dumps({k: m[k] for k in ("status", "duration_seconds", "errors", "warnings")}))
