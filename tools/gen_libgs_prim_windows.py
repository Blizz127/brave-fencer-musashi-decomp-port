#!/usr/bin/env python3
"""Print the libgs TMD primitive window digests for pc_port/mips_formatter.c.

Each span is cut into 64-word windows from its start; a window's digest is
FNV-1a 64 over its words' little-endian bytes in the licensed EXE, the same
hash code_site_group_ok uses. Digests only: no instruction word is emitted.

    tools/gen_libgs_prim_windows.py extracted/disc/files/SLUS_007.26
"""
import sys

# func_8004A27C's primitive entries (D_8004A2E0 .. D_8004CC24) and the second
# family D_8004D6BC .. D_800503F0 (asm/main.s labels).
SPANS = ((0x8004A27C, 0x8004CFEC), (0x8004D6BC, 0x800509C0))
BASE, TEXT = 0x80010000, 0x800


def fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h


def windows(exe):
    for lo, hi in SPANS:
        out = []
        for w in range(lo, hi, 256):
            end = min(w + 256, hi)
            out.append(fnv(exe[w - BASE + TEXT:end - BASE + TEXT]))
        yield lo, hi, out


def main():
    exe = open(sys.argv[1], "rb").read()
    for lo, hi, digests in windows(exe):
        print(f"static const uint64_t kLibgsPrim{lo:08X}[] = {{")
        for i in range(0, len(digests), 3):
            print("    " + " ".join(f"0x{d:016x}ull," for d in digests[i:i + 3]))
        print("};")


if __name__ == "__main__":
    main()
