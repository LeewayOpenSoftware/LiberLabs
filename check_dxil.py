#!/usr/bin/env python3
"""L19-dxc-l2 stage C, host side (Lab03): inspect the DXIL containers the VM wrote to C:\\l6\\dxil.

usage: python3 -I check_dxil.py <dir with *.dxil>
Checks the container header, the part set against the Microsoft dxc 1.8.2502.11 reference
set for the same fixtures, the DXIL program header (shader kind, shader model) and the
bitcode magic. No build, nothing executed. Exit code = number of failing files.
"""
import glob
import os
import struct
import sys

EXPECT_SM = {"cs60": (6, 0), "vs60": (6, 0), "ps60": (6, 0), "cs62": (6, 2), "cs65": (6, 5), "cs66": (6, 6)}
KIND = {0: "pixel", 1: "vertex", 5: "compute"}
REF_PARTS = {"SFI0", "ISG1", "OSG1", "PSV0", "STAT", "HASH", "DXIL"}
BC_MAGIC = b"BC\xc0\xde"


def check(path):
    data = open(path, "rb").read()
    name = os.path.basename(path)
    msg = []
    ok = data[:4] == b"DXBC" and len(data) >= 32
    if not ok:
        msg.append("not a DXIL container (%d bytes)" % len(data))
    else:
        count = struct.unpack_from("<I", data, 28)[0]
        offsets = struct.unpack_from("<%dI" % count, data, 32)
        parts = {}
        for off in offsets:
            parts[data[off:off + 4].decode("ascii", "replace")] = struct.unpack_from("<I", data, off + 4)[0]
        hash_zero = data[4:20] == bytes(16)
        msg.append("size=%d parts=%s hash=%s" % (len(data), ",".join(sorted(parts)), "zero(unsigned)" if hash_zero else "nonzero"))
        missing = REF_PARTS - set(parts)
        if missing:
            msg.append("missing-vs-reference=%s" % ",".join(sorted(missing)))
        if "DXIL" not in parts:
            ok = False
            msg.append("no DXIL part")
        else:
            off = [o for o in offsets if data[o:o + 4] == b"DXIL"][0] + 8
            # DxilProgramHeader: ProgramVersion, SizeInUint32, then DxilBitcodeHeader
            # ('DXIL', DxilVersion, BitcodeOffset, BitcodeSize), offsets relative to that header.
            pv = struct.unpack_from("<I", data, off)[0]
            kind, major, minor = pv >> 16, (pv >> 4) & 0xF, pv & 0xF
            magic = data[off + 8:off + 12]
            dxil_ver, bc_off, bc_size = struct.unpack_from("<III", data, off + 12)
            bc = data[off + 8 + bc_off:off + 8 + bc_off + 4]
            msg.append("kind=%s sm=%d.%d dxil=%d.%d bitcode=%d bytes %s" % (
                KIND.get(kind, kind), major, minor, dxil_ver >> 8, dxil_ver & 0xFF, bc_size,
                "magic-ok" if bc == BC_MAGIC else "BAD-MAGIC"))
            if magic != b"DXIL" or bc != BC_MAGIC:
                ok = False
            expected = EXPECT_SM.get(name[:4])
            if expected and (major, minor) != expected:
                ok = False
                msg.append("expected sm=%d.%d" % expected)
    print("DXIL-CHECK %s %s %s" % (name, "PASS" if ok else "FAIL", " ".join(msg)))
    return ok


def main():
    files = sorted(glob.glob(os.path.join(sys.argv[1], "*.dxil")))
    fails = sum(0 if check(f) else 1 for f in files)
    print("DXIL-CHECK-RESULT files=%d fail=%d" % (len(files), fails))
    return fails


if __name__ == "__main__":
    sys.exit(main())
