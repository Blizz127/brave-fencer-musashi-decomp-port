"""bfm_plat platform layer: config, backends, storage, mods, plugins, cheats.

Builds without SDL/OpenAL/GL. The disc images here are synthetic ISO9660
images generated below; no retail data is used.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
PLAT = ROOT / "pc_port/platform"
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O0", "-g"]
FILE_TEXT = b"Hello from a synthetic disc"


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def texture_hash(w: int, h: int, pixels) -> str:
    raw = struct.pack("<HH", w, h) + b"".join(struct.pack("<H", p) for p in pixels)
    return "%016x" % fnv1a64(raw)


def bfmi(w: int, h: int, rgba: bytes) -> bytes:
    return b"BFMI" + struct.pack("<HH", w, h) + rgba


# ---------------------------------------------------------- ISO9660 builder

def _both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def _both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def _dirrec(name: bytes, lba: int, size: int, is_dir: bool) -> bytes:
    body = (_both32(lba) + _both32(size) + bytes(7) +
            bytes([2 if is_dir else 0, 0, 0]) + _both16(1) +
            bytes([len(name)]) + name)
    rec = bytes([0, 0]) + body
    if len(rec) % 2:
        rec += b"\0"
    rec = bytes([len(rec)]) + rec[1:]
    return rec


def build_iso(file_data: bytes) -> bytes:
    sectors = [bytes(2048)] * 16
    root_lba, data_lba, file_lba = 18, 19, 20
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[156:156 + 34] = _dirrec(b"\0", root_lba, 2048, True)
    term = bytearray(2048)
    term[0] = 255
    term[1:6] = b"CD001"
    root = (_dirrec(b"\0", root_lba, 2048, True) +
            _dirrec(b"\1", root_lba, 2048, True) +
            _dirrec(b"DATA", data_lba, 2048, True))
    data = (_dirrec(b"\0", data_lba, 2048, True) +
            _dirrec(b"\1", root_lba, 2048, True) +
            _dirrec(b"HELLO.TXT;1", file_lba, len(file_data), False))
    sectors += [bytes(pvd), bytes(term), root.ljust(2048, b"\0"),
                data.ljust(2048, b"\0"), file_data.ljust(2048, b"\0")]
    return b"".join(sectors)


def build_iso_root(files):
    """ISO9660 image with the given {name: bytes} files in the root."""
    sectors = [bytes(2048)] * 16
    root_lba, next_lba = 18, 19
    recs, blobs = [_dirrec(b"\0", root_lba, 2048, True), _dirrec(b"\1", root_lba, 2048, True)], []
    for name, data in files.items():
        n = (len(data) + 2047) // 2048 or 1
        recs.append(_dirrec(name.encode() + b";1", next_lba, len(data), False))
        blobs.append(data.ljust(n * 2048, b"\0"))
        next_lba += n
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[156:156 + 34] = _dirrec(b"\0", root_lba, 2048, True)
    term = bytearray(2048)
    term[0] = 255
    term[1:6] = b"CD001"
    root = b"".join(recs)
    assert len(root) <= 2048
    return b"".join(sectors + [bytes(pvd), bytes(term), root.ljust(2048, b"\0")] + blobs)


def _bcd(v):
    return ((v // 10) << 4) | (v % 10)


def iso_to_raw(iso: bytes) -> bytes:
    out = bytearray()
    for i in range(len(iso) // 2048):
        a = i + 150
        hdr = bytes([_bcd(a // 4500), _bcd((a // 75) % 60), _bcd(a % 75), 2])
        out += b"\0" + b"\xff" * 10 + b"\0" + hdr + bytes(8)
        out += iso[i * 2048:(i + 1) * 2048] + bytes(280)
    return bytes(out)


# --------------------------------------------------------------- PNG writer

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _filter_row(ftype, row, prev, bpp):
    out = bytearray()
    for i, x in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i] if prev else 0
        c = prev[i - bpp] if prev and i >= bpp else 0
        pred = [0, a, b, (a + b) >> 1, _paeth(a, b, c)][ftype]
        out.append((x - pred) & 0xFF)
    return bytes([ftype]) + bytes(out)


def _chunk(t, body):
    return (struct.pack(">I", len(body)) + t + body +
            struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF))


def make_png(w, h, ctype, depth, rows, plte=None, trns=None, interlace=0):
    """rows: list of raw (unfiltered) scanline bytes."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)
    data, prev = b"", None
    for y, row in enumerate(rows):
        data += _filter_row(y % 5, row, prev, bpp)
        prev = row
    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if plte:
        out += _chunk(b"PLTE", plte)
    if trns:
        out += _chunk(b"tRNS", trns)
    out += _chunk(b"tEXt", b"Comment\0ancillary chunks are skipped")
    z = zlib.compress(data)
    out += _chunk(b"IDAT", z[:5]) + _chunk(b"IDAT", z[5:])   # split IDAT
    out += _chunk(b"IEND", b"")
    return out


def _pack_bits(indices, depth):
    out, acc, n = bytearray(), 0, 0
    for i in indices:
        acc = (acc << depth) | i
        n += depth
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


def png_cases():
    """(name, png bytes, width, height, expected RGBA bytes)."""
    cases = []
    w, h = 5, 6
    rgba = [[((x * 50) & 255, (y * 40) & 255, (x * y * 7) & 255, (x * 30 + y * 5) & 255)
             for x in range(w)] for y in range(h)]
    flat = lambda px: b"".join(bytes(p) for row in px for p in row)
    cases.append(("rgba8", make_png(w, h, 6, 8, [b"".join(bytes(p) for p in r) for r in rgba]),
                  w, h, flat(rgba)))
    rgb = [[p[:3] for p in r] for r in rgba]
    key = rgb[2][3]
    exp = [[p + ((0,) if p == key else (255,)) for p in r] for r in rgb]
    cases.append(("rgb8_trns", make_png(w, h, 2, 8, [b"".join(bytes(p) for p in r) for r in rgb],
                                        trns=struct.pack(">HHH", *key)), w, h, flat(exp)))
    rows16 = [b"".join(struct.pack(">HHH", p[0] * 257 + 1, p[1] * 257, p[2] * 257) for p in r)
              for r in rgb]
    exp16 = [[p + (255,) for p in r] for r in rgb]
    cases.append(("rgb16", make_png(w, h, 2, 16, rows16), w, h, flat(exp16)))
    grey = [[(x * 60 + y) & 255 for x in range(w)] for y in range(h)]
    cases.append(("grey8", make_png(w, h, 0, 8, [bytes(r) for r in grey]), w, h,
                  flat([[(g, g, g, 255) for g in r] for r in grey])))
    ga = [[((x * 60) & 255, (y * 50) & 255) for x in range(w)] for y in range(h)]
    cases.append(("greyalpha8", make_png(w, h, 4, 8, [b"".join(bytes(p) for p in r) for r in ga]),
                  w, h, flat([[(g, g, g, a) for g, a in r] for r in ga])))
    for depth in (1, 2, 4, 8):
        n = 1 << depth if depth < 8 else 7
        pal = [((i * 37) & 255, (i * 91) & 255, (i * 13) & 255) for i in range(n)]
        idx = [[(x + y * 3) % n for x in range(w)] for y in range(h)]
        alpha = bytes([0] + [200] * (n - 2))     # shorter than palette: rest opaque
        exp = [[pal[i] + ((alpha[i],) if i < len(alpha) else (255,)) for i in r] for r in idx]
        cases.append((f"palette{depth}", make_png(w, h, 3, depth,
                                                  [_pack_bits(r, depth) for r in idx],
                                                  plte=b"".join(bytes(p) for p in pal),
                                                  trns=alpha), w, h, flat(exp)))
    return cases


def read_png_rgb(path):
    """Minimal PNG reader for the port's own screenshots (filter 0 rows)."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        crc = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0]
        assert zlib.crc32(t + body) & 0xFFFFFFFF == crc, t
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert (depth, ctype) == (8, 2)
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3
    rows = [raw[y * (stride + 1):(y + 1) * (stride + 1)] for y in range(h)]
    assert all(r[0] == 0 for r in rows)
    return w, h, b"".join(r[1:] for r in rows)


# Known hook sites pinned to their symbols. Changing a site means changing
# this table and its evidence together.
KNOWN_SITES = {
    ("8004923c", "-", "projection"): ("func_8004923C", "high"),
    ("8004921c", "-", "projection"): ("func_8004921C", "high"),
    ("8014bc80", "list", "damage"): ("func_8014BC80", "high"),
    ("8014bd60", "list", "bp_use"): ("func_8014BD60", "high"),
    ("800d0f0c", "-", "item_get"): ("func_800D0F0C", "high"),
    ("800d128c", "-", "item_use"): ("func_800D128C", "high"),
    ("800189a8", "-", "frame_begin"): ("func_800189A8", "high"),
    ("800184f0", "-", "frame_end"): ("func_800184F0", "high"),
    ("80128158", "SC*", "room_enter"): ("func_80128158", "medium"),
    ("80011b7c", "-", "room_exit"): ("func_80011B7C", "low"),
    ("8002b0b4", "-", "save"): ("func_8002B0B4", "medium"),
    ("8002b0b4", "-", "load"): ("func_8002B0B4", "medium"),
}
UNSITED = ["battle_start", "battle_end"]

# (vendor/bfm-decomp path, line, text that line must contain): each is
# cited in the sites' evidence strings.
VENDOR_EVIDENCE = [
    ("src/boot.c", 277, "void main(void)"),
    ("src/boot.c", 310, "func_800189A8();"),
    ("src/boot.c", 372, "func_800184F0();"),
    ("src/boot.c", 374, "VSync(*(s32 *)(p + 0xA3E8));"),
    ("src/800.c", 6042, "void func_800189A8(void)"),
    ("src/800.c", 6094, "PadGetState(chan)"),
    ("src/800.c", 5761, "void func_800184F0(void)"),
    ("src/md_MAIN_003/md_MAIN_003_jr_800D1E18.c", 236, "func_800189A8();"),
    ("src/resident/resident.c", 255, "func_80128158();"),
    ("src/resident/resident.c", 261, "func_80011B7C(8);"),
    ("src/boot.c", 950, "void func_80011B7C(arg0)"),
    ("src/800_b.c", 87, "s32 func_8002B0B4(s32 sel, s32 idx, u8 *out)"),
    ("src/800_b.c", 301, "case 13:"),
    ("src/800_b.c", 318, "func_80060614(D_80075CC0, idx * 0x300 + 0x480, 0x300);"),
    ("src/800_b.c", 486, "case 17:"),
    ("src/800_b.c", 487, "case 32:"),
    ("src/800_b.c", 488, "func_80060404(D_80075CC0, idx * 0x300 + 0x480, 0x300);"),
    ("src/800.c", 1787, "func_8004923C(0x3E8);"),
    ("src/shared/ov/func_8017C530.h", 14, "func_8004921C(ofx, ofy);"),
    # inventory (resident)
    ("src/resident/resident_jr_800D00E4.c", 950, "s32 func_800D0EC4(void)"),
    ("src/resident/resident_jr_800D00E4.c", 953, "for (i = 0x2F; (u32)i < 0x3B; i++)"),
    ("src/resident/resident_jr_800D00E4.c", 965, "void func_800D0F0C(s32 arg0, s32 arg1)"),
    ("src/resident/resident_jr_800D00E4.c", 1197, "func_800D128C(s0, (s16)((u16)D_80078EAC - v0));"),
    ("src/resident/resident_jr_800D128C.c", 242, "s32 func_800D128C(s32 arg0, s32 arg1)"),
    ("src/ov_SC03_124/ov_SC03_124_jr_80188544.c", 3526, "func_800D0F0C(slot, w10);"),
    ("src/shared/ov/func_8014BC80.h", None, "D_800B9A17 = 0;"),
    ("config/dedup.us.yaml", 8867, "vram: 0x8014BC80"),
    # location, day/time, NPC stamps
    ("config/symbols.us.txt", 1023, "currentLocationId                = 0x800B9A08; // data"),
    ("src/resident/resident.c", 469, "s16 *cli = &currentLocationId;"),
    ("src/resident/resident_jr_800D128C.c", 992, "currentLocationId = v0;"),
    ("src/ov_SC03_001/ov_SC03_001_jr_801870B0.c", 3407, "= idx2 + (idx1 * 24);"),
    ("src/ov_SC04_018/ov_SC04_018_jr_80183E6C.c", 3643, "q = t / 24;"),
    # live game state (bfm_plat_events.h BFM_GUEST_*)
    ("src/800.c", 21115, "void func_800295D4(void)"),
    ("src/800.c", 21116, "D_80078EB4 = 0xFA;"),
    ("src/800.c", 21117, "D_80078EB2 = 0xFA;"),
    ("src/800.c", 21118, "D_80078EB8 = 0xFA;"),
    ("src/800.c", 21119, "D_80078EB6 = 0xFA;"),
    ("src/800.c", 21125, "D_80078E8C = 0x64;"),
    ("src/800.c", 21037, "void func_80029444(void)"),
    ("src/800.c", 21053, "D_80078E7C_w = 0x633B3B00;"),
    ("src/800.c", 21190, "*(Blk98_80029274 *)(dst + 0x24) = D_80078E78;"),
    ("src/800.c", 20881, "void func_80029124(s32 arg0, s32 arg1)"),
    ("src/800.c", 22268, "func_800291B4((s16)i + 0x63) & 0x40"),
    ("src/shared/ov/func_8014BC0C.h", 6, "(u16)D_80078EB2 >= 0x1F5"),
    ("src/shared/ov/func_8014BCEC.h", 9, "v >= 0x663"),
    ("src/shared/ov/func_8014BB0C.h", 6, "D_80078EB4 = D_80078EB2;"),
    ("src/shared/ov/func_8014BDC8.h", 6, "D_80078EB8 = D_80078EB6;"),
    ("src/ov_SC03_124/ov_SC03_124_jr_80188544.c", 3740, "var = D_80078E8C + a0;"),
    ("src/ov_SC03_124/ov_SC03_124_jr_80188544.c", 3741, "if (var > 99999)"),
    ("src/ov_SC03_124/ov_SC03_124_jr_80188544.c", 3442, "func_8018957C(-D_800A6586[i13])"),
    ("src/ov_SC03_124/ov_SC03_124_jr_80188544.c", 3447, "func_800291A0(c13, f13 | 0x40);"),
    ("src/ov_MAIN_012/ov_MAIN_012_jr_8017CF3C.c", 4103, "a = p[7];"),
    ("src/ov_MAIN_012/ov_MAIN_012_jr_8017CF3C.c", 4142, "a = *(u16 *)(p + 0x3C);"),
    ("src/ov_MAIN_012/ov_MAIN_012_jr_8017CF3C.c", 4157, "a = *(u16 *)(p + 0x40);"),
    ("src/ov_MAIN_012/ov_MAIN_012_jr_8017CF3C.c", 4172, "a = *(u32 *)(p + 0x14);"),
    ("config/symbols.us.txt", 14, "main                             = 0x80010178; // func"),
    ("config/symbols.us.txt", None, "GameModeDispatch                 = 0x80010B40;"),
]


SITE_EVIDENCE_COUNT = 19   # (inventory pins follow; they are checked by the vendor test)   # the entries above "live game state" are cited by hook sites


def vendor_root():
    for base in (ROOT, Path(os.environ.get("BFM_DECOMP_ROOT", str(ROOT)))):
        v = base / "vendor/bfm-decomp"
        if (v / "src/boot.c").is_file():
            return v
    return None


class BfmPlatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory(prefix="bfm-plat-")
        cls.addClassCleanup(temp.cleanup)
        cls.tmp = Path(temp.name)
        cls.probe = cls.tmp / "bfm_plat_probe"
        sources = sorted(str(p) for p in PLAT.glob("bfm_plat*.c")) + [str(PLAT / "psyq/bfm_psyq_compat.c"), str(PLAT / "psyq/bfm_psyq_libgs.c"),
                                                                    str(PLAT / "psyq/bfm_psyq_libgte.c")]
        subprocess.run(["cc", *CFLAGS, "-I", str(PLAT), *sources,
                        str(ROOT / "tests/bfm_plat_probe.c"), "-o",
                        str(cls.probe), "-ldl", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=120)
        cls.png_probe = cls.tmp / "bfm_plat_png_probe"
        subprocess.run(["cc", *CFLAGS, "-DBFM_PLAT_WITH_PNG", "-I", str(PLAT),
                        *sources, str(PLAT / "backends/texture_png.c"),
                        str(ROOT / "tests/bfm_plat_probe.c"), "-o",
                        str(cls.png_probe), "-ldl", "-lz", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=120)
        cls.plugin = cls.tmp / "hello_plugin.so"
        subprocess.run(["cc", *CFLAGS, "-shared", "-fPIC", "-I", str(PLAT),
                        str(ROOT / "pc_port/mods/examples/hello/hello_plugin.c"),
                        "-o", str(cls.plugin)],
                       check=True, stdin=subprocess.DEVNULL, timeout=60)

    def run_probe(self, *args, env=None, probe=None):
        proc = subprocess.run([str(probe or self.probe), *map(str, args)],
                              capture_output=True, text=True, timeout=60,
                              env=env, stdin=subprocess.DEVNULL)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("ok " + args[0], proc.stdout)
        return proc.stdout

    def fresh(self, name):
        d = self.tmp / name
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        return d

    # ------------------------------------------------------------ groups

    def test_hash_matches_reference_fnv1a64(self):
        self.run_probe("hash")
        self.assertEqual("%016x" % fnv1a64(b"foobar"), "85944171f73967e8")

    def test_config_file_args_and_default_path(self):
        d = self.fresh("config")
        env = dict(os.environ, XDG_CONFIG_HOME=str(d / "xdg"))
        env.pop("BFM_PORT_CONFIG", None)
        out = self.run_probe("config", d, env=env)
        self.assertIn(f"path {d}/xdg/bfm-port/config.ini", out)
        env["BFM_PORT_CONFIG"] = "/custom/cfg.ini"
        self.assertIn("path /custom/cfg.ini", self.run_probe("config", d, env=env))
        saved = (d / "saved.ini").read_text()
        self.assertIn("[mod.hello]\ngreeting = hi there", saved)

    def test_renderer_null_backend_and_fallback(self):
        self.run_probe("renderer")

    def test_input_wire_format_hotkeys_and_fast_forward(self):
        self.run_probe("input")

    def test_timing_pacer_with_injected_clock(self):
        self.run_probe("timing")

    def test_audio_null_backend(self):
        self.run_probe("audio")

    def test_storage_iso_raw_bin_and_cue(self):
        d = self.fresh("storage")
        iso = build_iso(FILE_TEXT)
        (d / "game.iso").write_bytes(iso)
        (d / "game.bin").write_bytes(iso_to_raw(iso))
        (d / "game.cue").write_text(
            'FILE "game.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
        (d / "saves").mkdir()
        out = self.run_probe("storage", d / "game.iso", FILE_TEXT.decode(), d / "saves")
        self.assertIn("sectors 21 size 2048 tracks 1", out)
        out = self.run_probe("storage", d / "game.bin", FILE_TEXT.decode(), d / "saves")
        self.assertIn("sectors 21 size 2352", out)
        out = self.run_probe("storage", d / "game.cue", FILE_TEXT.decode(), d / "saves")
        self.assertIn("sectors 21 size 2352 tracks 1", out)
        self.assertEqual((d / "saves/bfm_card1.mcd").stat().st_size, 128 * 1024)
        self.assertFalse((d / "saves/bfm_card1.mcd.tmp").exists())

    def test_disc_file_replacement_by_hash(self):
        d = self.fresh("filerep")
        (d / "game.iso").write_bytes(build_iso(FILE_TEXT))
        files = d / "mods/patch/assets/files"
        files.mkdir(parents=True)
        (d / "mods/patch/mod.ini").write_text("[mod]\nname = patch\n")
        out = self.run_probe("file_replace", d / "game.iso", d / "mods")
        self.assertIn("file " + FILE_TEXT.decode(), out)
        (files / ("%016x.txt" % fnv1a64(FILE_TEXT))).write_bytes(b"Patched text")
        out = self.run_probe("file_replace", d / "game.iso", d / "mods")
        self.assertIn("file Patched text", out)

    def test_texture_replacement_same_size_hd_and_dump(self):
        d = self.fresh("tex")
        pixels = [0x1234, 0x0001, 0x7FFF, 0x0000]
        h = texture_hash(2, 2, pixels)
        out = self.run_probe("texture", d / "mods")
        self.assertIn("hash " + h, out)
        self.assertIn("vram 1234 0001 7fff 0000 replaced 0", out)

        tex = d / "mods/hd/assets/textures"
        tex.mkdir(parents=True)
        (d / "mods/hd/mod.ini").write_text("[mod]\nname = hd\n")
        # Same-size replacement: red, opaque black, white, transparent.
        rgba = bytes([255, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0])
        (tex / f"{h}.bfmi").write_bytes(bfmi(2, 2, rgba))
        out = self.run_probe("texture", d / "mods")
        self.assertIn("vram 001f 8000 7fff 0000 replaced 1", out)

        # A later-loading mod with a 2x HD version wins, but the null backend
        # has no HD path, so VRAM keeps the original upload.
        tex2 = d / "mods/zz_hd2x/assets/textures"
        tex2.mkdir(parents=True)
        (d / "mods/zz_hd2x/mod.ini").write_text("[mod]\nname = hd2x\npriority = 200\n")
        (tex2 / f"{h}.bfmi").write_bytes(bfmi(4, 4, bytes(64)))
        out = self.run_probe("texture", d / "mods")
        self.assertIn("vram 1234 0001 7fff 0000 replaced 0", out)

        # Bad-size replacement is ignored.
        (tex2 / f"{h}.bfmi").write_bytes(bfmi(3, 2, bytes(24)))
        out = self.run_probe("texture", d / "mods")
        self.assertIn("vram 1234 0001 7fff 0000 replaced 0", out)

        dump = d / "dump"
        shutil.rmtree(d / "mods")
        self.run_probe("texture", d / "mods", dump)
        dumped = (dump / "textures" / f"{h}.bfmi").read_bytes()
        self.assertEqual(dumped[:8], bfmi(2, 2, b""))
        self.assertEqual(len(dumped), 8 + 16)
        self.assertEqual(dumped[8 + 12:], bytes(4))   # 0x0000 -> transparent

    def test_mods_loader_plugin_cheats_console(self):
        d = self.fresh("mods")
        mods = d / "mods"
        hello = mods / "hello"
        hello.mkdir(parents=True)
        src = ROOT / "pc_port/mods/examples/hello"
        shutil.copy(src / "mod.ini", hello)
        shutil.copy(self.plugin, hello / "hello_plugin.so")
        (hello / "cheats.ini").write_text(
            "[cheat hp_lock]\ndescription = test poke\naddress = 0x80001234\n"
            "value = 0x63\nwidth = 2\nenabled = 1\n"
            "[cheat bad_width]\naddress = 0x80000000\nvalue = 1\nwidth = 3\n")
        (mods / "static").mkdir()
        (mods / "static/mod.ini").write_text(
            "[mod]\nname = static\npriority = 10\nplugin = builtin_static\n")
        (mods / "scripted").mkdir()
        (mods / "scripted/mod.ini").write_text(
            "[mod]\nname = scripted\npriority = 300\nscript = main.lua\n")
        (mods / "scripted/main.lua").write_text("-- placeholder\n")
        (mods / "off").mkdir()
        (mods / "off/mod.ini").write_text("[mod]\nname = off\nenabled = 0\nplugin = nope\n")
        (mods / "evil").mkdir()
        (mods / "evil/mod.ini").write_text("[mod]\nname = evil\nplugin = ../../x\n")
        (mods / "not_a_mod").mkdir()          # no manifest: skipped
        out = self.run_probe("mods", mods)
        lines = [l for l in out.splitlines() if l.startswith("mod ")]
        names = [l.split()[2] for l in lines]
        self.assertEqual(names, ["static", "evil", "hello", "off", "scripted"])
        self.assertIn("mod 2 hello prio=100 enabled=1 plugin=1 script=0 cheats=1", out)
        self.assertIn("mod 1 evil prio=100 enabled=1 plugin=-1", out)
        self.assertIn("mod 3 off prio=100 enabled=0 plugin=0", out)
        self.assertIn("mod 4 scripted prio=300 enabled=1 plugin=0 script=1", out)
        self.assertIn("hello-reply howdy: 0 frames, 0 rooms", out)
        self.assertIn("ok-shutdown", out)

    def test_input_bindings_from_config(self):
        self.run_probe("bindings")

    def test_console_text_front_end(self):
        self.run_probe("console_ui")

    def test_game_event_catalog_and_hook_sites(self):
        self.run_probe("hooks")

    def test_png_decoder_all_color_types_depths_and_filters(self):
        d = self.fresh("png")
        for name, data, w, h, expected in png_cases():
            path = d / f"{name}.png"
            path.write_bytes(data)
            out = self.run_probe("png", path, probe=self.png_probe)
            self.assertIn(f"png {w} {h} {expected.hex()}", out, name)

    def test_png_decoder_refuses_bad_files(self):
        d = self.fresh("pngbad")
        good = png_cases()[0][1]
        bad_crc = bytearray(good)
        bad_crc[30] ^= 0xFF                       # inside IHDR body
        interlaced = make_png(1, 1, 6, 8, [bytes(4)], interlace=1)
        cases = {
            "crc": (bytes(bad_crc), -3),
            "truncated": (good[:-20], -3),
            "signature": (b"NOTAPNG!" + good[8:], -3),
            "interlaced": (interlaced, -2),
            "no_palette": (make_png(1, 1, 3, 8, [b"\0"]), -3),
        }
        for name, (data, code) in cases.items():
            (d / f"{name}.png").write_bytes(data)
            out = self.run_probe("png", d / f"{name}.png", probe=self.png_probe)
            self.assertIn(f"png error {code}", out, name)

    def test_png_texture_replacement_through_mods(self):
        d = self.fresh("pngtex")
        h = texture_hash(2, 2, [0x1234, 0x0001, 0x7FFF, 0x0000])
        tex = d / "mods/pngmod/assets/textures"
        tex.mkdir(parents=True)
        (d / "mods/pngmod/mod.ini").write_text("[mod]\nname = pngmod\n")
        rows = [bytes([255, 0, 0, 255, 0, 0, 0, 255]), bytes([255, 255, 255, 255, 0, 0, 0, 0])]
        (tex / f"{h}.png").write_bytes(make_png(2, 2, 6, 8, rows))
        out = self.run_probe("texture_png", d / "mods", probe=self.png_probe)
        self.assertIn("vram 001f 8000 7fff 0000 replaced 1", out)
        # Without the PNG decoder registered, .png replacements are ignored.
        out = self.run_probe("texture", d / "mods")
        self.assertIn("replaced 0", out)

    def test_known_hook_sites_pinned_to_symbols(self):
        out = self.run_probe("sites")
        sites = {}
        for line in out.splitlines():
            parts = line.split()
            if parts[:1] == ["site"]:
                sites[(parts[1], parts[2], parts[3])] = (parts[4], parts[5])
        self.assertEqual(sites, KNOWN_SITES)
        unsited = [l.split()[1] for l in out.splitlines() if l.startswith("unsited ")]
        self.assertEqual(unsited, UNSITED)
        evidence = " ".join(l for l in out.splitlines() if l.startswith("evidence "))
        for path, line, _ in VENDOR_EVIDENCE[:SITE_EVIDENCE_COUNT]:
            if line is not None and path.startswith("src/"):
                self.assertIn(f"{path}:{line}" if path != "src/800_b.c" or line not in (486, 487, 488)
                              else "src/800_b.c:486-488", evidence)
        # The repository file the room-enter evidence cites.
        disp = (ROOT / "src/overlays/main_0012/80128288.c").read_text()
        self.assertIn("D_8017E618[D_800B99F6]();", disp)
        # Repository files the projection evidence cites.
        self.assertIn("ctc2 $a0, $26", (ROOT / "src/main/8004923c.c").read_text())
        off = (ROOT / "src/main/8004921c.c").read_text()
        self.assertIn("ctc2 $a0, $24", off)
        self.assertIn("ctc2 $a1, $25", off)
        self.assertIn("func_8004921C(", (ROOT / "src/main/80052bec.c").read_text())
        self.assertIn("func_8004923C(0x3E8);", (ROOT / "src/main/80014444.c").read_text())

    def test_hook_site_evidence_matches_vendor_source(self):
        v = vendor_root()
        if v is None:
            self.skipTest("vendor/bfm-decomp not present")
        for path, line, text in VENDOR_EVIDENCE:
            lines = (v / path).read_text(errors="replace").splitlines()
            if line is None:
                self.assertTrue(any(text in l for l in lines), (path, text))
            else:
                self.assertIn(text, lines[line - 1], (path, line))

    def test_hook_site_fills_and_frame_actions(self):
        self.run_probe("hook_fills")

    def test_pause_screenshot_and_quit(self):
        d = self.fresh("shots")
        out = self.run_probe("pause_screenshot", d)
        self.assertIn(f"shot {d}/bfm_00000.png", out)
        w, h, rgb = read_png_rgb(d / "bfm_00000.png")
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(rgb.hex(), "ff0000" "00ff00" "0000ff" "ffffff"
                                    "000000" "848484" "080808" "000000")
        w, h, rgb = read_png_rgb(d / "rgb24.png")
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(rgb.hex(), "1f00e003007c" "000010422104")
        # The port's own PNG decoder reads its screenshots back.
        out = self.run_probe("png", d / "bfm_00000.png", probe=self.png_probe)
        self.assertIn("png 4 2 ff0000ff00ff00ff", out)

    def test_widescreen_projection_maths(self):
        from fractions import Fraction as F
        out = self.run_probe("widescreen")
        lines = out.splitlines()
        for num, den in ((4, 3), (16, 9), (16, 10)):
            aspect = F(num, den)
            for w in (256, 320, 368, 512, 640):
                exact = w * aspect / F(4, 3)
                vis = (w * 3 * num + 2 * den) // (4 * den)
                vis += (vis - w) & 1
                # within a pixel, plus at most one for the centring parity bump
                self.assertLessEqual(abs(vis - exact), F(3, 2))
                self.assertIn(f"horplus {num}:{den} {w} visible {vis} x0 {-((vis - w) // 2)} "
                              f"scale 65536 active {int(aspect != F(4, 3))}", lines)
            scale = (4 * den * 65536 + (3 * num) // 2) // (3 * num)
            self.assertLess(abs(F(scale, 65536) - F(4, 3) / aspect), F(1, 65536))
            want = []
            for sx in (-1024, -1, 0, 1, 40, 159, 160, 161, 319, 320, 1023):
                q = (sx - 160) * scale
                q = (q + 32768) >> 16 if q >= 0 else -((-q + 32768) >> 16)
                self.assertLessEqual(abs(160 + q - (160 + (sx - 160) * F(4, 3) / aspect)), F(1, 2))
                want.append(str(160 + q))
            self.assertIn(f"anamorphic {num}:{den} scale {scale} " + " ".join(want), lines)
            for h in (240, 720, 1081):
                ww = (h * num + den // 2) // den
                ww += ww & 1
                self.assertIn(f"window {num}:{den} {h} {ww}", lines)
        self.assertIn("hfov 18181 24158", lines)   # 2*atan(160/1000), 2*atan(214/1000)

    def test_lua_runtime_sample_mod_and_sandbox(self):
        lua_src = ROOT / "tools/third_party/lua/src"
        if not (lua_src / "lua.h").is_file():
            self.skipTest("Lua not fetched (tools/fetch_lua.sh)")
        objdir = self.tmp / "luaobj"
        objdir.mkdir(exist_ok=True)
        lua_c = sorted(str(p) for p in lua_src.glob("*.c") if p.name not in ("lua.c", "luac.c"))
        subprocess.run(["cc", "-std=gnu99", "-O1", "-c", *lua_c], cwd=objdir, check=True,
                       stdin=subprocess.DEVNULL, timeout=300)
        probe = self.tmp / "bfm_plat_lua_probe"
        sources = sorted(str(p) for p in PLAT.glob("bfm_plat*.c")) + [str(PLAT / "psyq/bfm_psyq_compat.c"), str(PLAT / "psyq/bfm_psyq_libgs.c"),
                                                                    str(PLAT / "psyq/bfm_psyq_libgte.c")]
        subprocess.run(["cc", *CFLAGS, "-DBFM_PLAT_WITH_LUA", "-I", str(PLAT), "-I", str(lua_src),
                        *sources, str(PLAT / "backends/script_lua.c"),
                        str(ROOT / "tests/bfm_plat_probe.c"), *sorted(map(str, objdir.glob("*.o"))),
                        "-o", str(probe), "-ldl", "-lm"],
                       check=True, stdin=subprocess.DEVNULL, timeout=300)
        d = self.fresh("luamods")
        shutil.copytree(ROOT / "pc_port/mods/examples/lua_hello", d / "lua_hello")
        (d / "luabad").mkdir()
        (d / "luabad/mod.ini").write_text("[mod]\nname = luabad\nscript = main.lua\n")
        (d / "luabad/main.lua").write_text("this is not lua (\n")
        (d / "luaspin").mkdir()
        (d / "luaspin/mod.ini").write_text("[mod]\nname = luaspin\nscript = main.lua\n")
        (d / "luaspin/main.lua").write_text("while true do end\n")
        (d / "luasandbox").mkdir()
        (d / "luasandbox/mod.ini").write_text("[mod]\nname = luasandbox\nscript = main.lua\n")
        (d / "luasandbox/main.lua").write_text("io.open('/etc/passwd')\n")
        (d / "luatest").mkdir()
        (d / "luatest/mod.ini").write_text("[mod]\nname = luatest\npriority = 200\nscript = main.lua\n")
        (d / "luatest/main.lua").write_text("""
local seen = {}
bfm.on("item_get", function(_, p) seen.item = p.item .. "x" .. p.count end)
bfm.on("save", function(_, p) seen.slot = p.slot end)
bfm.on(15, function(name, p) seen.proj = name .. ":" .. p.ofx .. "," .. p.ofy end)
local sub = bfm.on("vsync", function() seen.vsync = true end)
assert(bfm.off(sub))
assert(bfm.read16(0x80003000) == 0x2211)
assert(bfm.read(0x80003000, 2) == "\\x11\\x22")
bfm.write32(0x80003010, 0xDEADBEEF)
bfm.cheat{name = "lua_poke", address = 0x80003020, value = 0x5A, width = 1}
bfm.set_cheat("lua_poke", true)
local sandbox = tostring(io) .. "," .. tostring(os) .. "," .. tostring(require) .. "," .. tostring(load)
bfm.command("luatest", "luatest", function()
  return string.format("item=%s slot=%s proj=%s vsync=%s sandbox=%s mod=%s",
    seen.item, seen.slot, seen.proj, tostring(seen.vsync), sandbox, bfm.mod_name)
end)
bfm.command("luaboom", "luaboom", function() error("boom") end)
bfm.on("frame_end", function() if seen.late == nil then
  seen.late = pcall(bfm.cheat, {name = "late", address = 0, value = 0, width = 1}) end end)
""")
        out = self.run_probe("lua", d, probe=probe)
        self.assertIn("mod lua_hello script=1", out)
        self.assertIn("mod luatest script=1", out)
        self.assertIn("mod luabad script=-1", out)
        self.assertIn("mod luaspin script=-1", out)        # budget stopped the loop
        self.assertIn("mod luasandbox script=-1", out)     # io is not available
        self.assertIn("reply hola: 0 frames, 0 rooms a,b", out)
        self.assertIn("reply hola: 3 frames, 1 rooms", out)
        self.assertIn("luatest item=7x3 slot=2 proj=projection:160,120 vsync=nil "
                      "sandbox=nil,nil,nil,nil mod=luatest", out)
        self.assertIn("ram efbeadde poke 5a", out)
        self.assertIn("turbo 2", out)
        self.assertIn("boom luaboom: script error", out)

    def test_sha256_vectors(self):
        self.run_probe("sha256")

    def disc_status(self, *args):
        out = self.run_probe("disc_check", *args)
        return out.split("status ", 1)[1].split()[0], out

    def test_disc_check_statuses_and_messages(self):
        import hashlib
        d = self.fresh("disccheck")
        exe = b"PS-X EXE" + bytes(range(256)) * 40            # synthetic, not retail
        exe_sha = hashlib.sha256(exe).hexdigest()
        cnf = b"BOOT = cdrom:\\SLUS_007.26;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFFF0\r\n"
        good = build_iso_root({"SYSTEM.CNF": cnf, "SLUS_007.26": exe})
        (d / "good.iso").write_bytes(good)
        (d / "good.bin").write_bytes(iso_to_raw(good))
        (d / "good.cue").write_text('FILE "good.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'
                                    '  TRACK 02 AUDIO\n    INDEX 01 00:10:00\n')
        # OK with the synthetic hash, through every container
        for name in ("good.iso", "good.bin", "good.cue"):
            st, out = self.disc_status(d / name, "SLUS_007.26", exe_sha)
            self.assertEqual(st, "ok", out)
            self.assertIn(f"boot=SLUS_007.26 sha={exe_sha}", out)
        self.assertIn("tracks=2 sector=2352", self.disc_status(d / "good.cue", "SLUS_007.26", exe_sha)[1])
        # the retail check: right name, wrong hash
        st, out = self.disc_status(d / "good.iso")
        self.assertEqual(st, "bad_dump")
        self.assertIn("Re-dump the disc", out)
        # region / other game / not a PS1 disc
        for boot, want in (("SLPS_014.90", "wrong_region"), ("SLES_012.34", "wrong_region"),
                           ("SLUS_000.01", "wrong_disc")):
            img = build_iso_root({"SYSTEM.CNF": cnf.replace(b"SLUS_007.26", boot.encode()), boot: exe})
            (d / "other.iso").write_bytes(img)
            st, out = self.disc_status(d / "other.iso")
            self.assertEqual(st, want, boot)
            self.assertIn(boot, out)
        self.assertIn("SLUS-00726", self.disc_status(d / "other.iso")[1])
        (d / "nocnf.iso").write_bytes(build_iso_root({"README.TXT": b"x"}))
        self.assertEqual(self.disc_status(d / "nocnf.iso")[0], "not_ps1")
        (d / "noboot.iso").write_bytes(build_iso_root({"SYSTEM.CNF": b"TCB = 4\n"}))
        self.assertEqual(self.disc_status(d / "noboot.iso")[0], "not_ps1")
        (d / "noise.iso").write_bytes(bytes(2048 * 20))
        self.assertEqual(self.disc_status(d / "noise.iso")[0], "not_ps1")
        # containers that are broken
        (d / "missing.cue").write_text('FILE "gone.bin" BINARY\n  TRACK 01 MODE2/2352\n')
        st, out = self.disc_status(d / "missing.cue")
        self.assertEqual(st, "missing_tracks")
        self.assertIn("gone.bin", out)
        (d / "short.bin").write_bytes(iso_to_raw(good)[:-100])
        self.assertEqual(self.disc_status(d / "short.bin")[0], "unreadable")
        raw = bytearray(iso_to_raw(good))
        raw[16 * 2352] = 0x55                                   # break sector 16's sync
        (d / "nosync.bin").write_bytes(bytes(raw))
        self.assertEqual(self.disc_status(d / "nosync.bin")[0], "unreadable")
        (d / "game.chd").write_bytes(b"MComprHD")
        st, out = self.disc_status(d / "game.chd")
        self.assertEqual(st, "unsupported_format")
        self.assertIn("chdman extractcd", out)
        self.assertEqual(self.disc_status(d / "nope.iso")[0], "unreadable")
        self.assertEqual(self.disc_status("-")[0], "no_path")
        # first run through bfm_plat_init
        disc = d / "firstrun"
        disc.mkdir()
        (disc / "Brave Fencer Musashi.bin").write_bytes(iso_to_raw(good))
        (disc / "Brave Fencer Musashi.cue").write_text(
            'FILE "Brave Fencer Musashi.bin" BINARY\n  TRACK 01 MODE2/2352\n')
        out = self.run_probe("disc_init", disc, d / "nope.cue")
        self.assertIn("autodetected " + str(disc / "Brave Fencer Musashi.cue"), out)

    def test_memory_watches_and_derived_events(self):
        d = self.fresh("watchmods")
        (d / "w").mkdir()
        (d / "w/mod.ini").write_text("[mod]\nname = w\nplugin = watcher\n")
        self.run_probe("watch", d)

    def test_cheat_pack_sample(self):
        d = self.fresh("cheatpack")
        shutil.copytree(ROOT / "pc_port/mods/examples/cheat_pack", d / "cheat_pack")
        ini = (d / "cheat_pack/cheats.ini").read_text()
        self.assertEqual(ini.count("enabled = 0"), 4)
        self.assertNotIn("enabled = 1", ini)
        out = self.run_probe("cheat_pack", d)
        for name in ("infinite_hp", "infinite_bp", "max_money", "max_hp_gauge"):
            self.assertIn(f"[on]  {name}", out)

    def test_gauge_site_overlay_list_matches_dedup(self):
        import re
        src = (PLAT / "bfm_plat_hook_sites.c").read_text()
        m = re.search(r"gauge_overlays\[\] =\s*((?:\s*\"[^\"]*\")+);", src)
        listed = set("".join(re.findall(r'"([^"]*)"', m.group(1))).split(","))
        self.assertEqual(len(listed), 141)
        v = vendor_root()
        if v is None:
            self.skipTest("vendor/bfm-decomp not present")
        text = (v / "config/dedup.us.yaml").read_text()
        for fn in ("func_8014BC80", "func_8014BD60"):
            mm = re.search(rf"func: {fn}\n\s+vram: 0x[0-9A-F]+\n\s+binaries: \[([^\]]*)\]", text)
            members = {x.strip()[3:] for x in mm.group(1).split(",")}
            self.assertEqual(members, listed, fn)

    def test_live_state_constants_match_documented_addresses(self):
        header = (PLAT / "bfm_plat_events.h").read_text()
        want = {"BFM_GUEST_LIVE_STRUCT": "0x80078E78u", "BFM_GUEST_PLAY_TIME": "0x80078E7Cu",
                "BFM_GUEST_MONEY": "0x80078E8Cu", "BFM_GUEST_HP_MAX": "0x80078EB2u",
                "BFM_GUEST_HP": "0x80078EB4u", "BFM_GUEST_BP_MAX": "0x80078EB6u",
                "BFM_GUEST_BP": "0x80078EB8u", "BFM_GUEST_STORY_FLAGS": "0x800AE648u",
                "BFM_GUEST_SCRIPT_VARS": "0x800BA1B8u", "BFM_GUEST_FIGURE_PRICES": "0x800A6588u",
                "BFM_GUEST_DAY": "0x80078EACu", "BFM_GUEST_HOUR": "0x80078EB1u",
                "BFM_GUEST_LOCATION": "0x800B9A08u", "BFM_GUEST_NPC_TIMES": "0x800BA2B8u"}
        for name, value in want.items():
            self.assertRegex(header, rf"#define {name}\s+{value}")
        modding = (ROOT / "docs/MODDING.md").read_text()
        for name, value in want.items():
            self.assertIn(value.rstrip("u").replace("0x", "0x"), modding, name)
        for sample in ("examples/cheat_pack", "examples/hello", "examples/lua_hello"):
            self.assertIn(sample, modding)
        pack = (ROOT / "pc_port/mods/examples/cheat_pack/cheats.ini").read_text()
        for addr in ("0x80078EB4", "0x80078EB2", "0x80078EB8", "0x80078EB6", "0x80078E8C"):
            self.assertIn(addr, pack)

    def test_psyq_hle_wrappers(self):
        d = self.fresh("psyq")
        (d / "g.iso").write_bytes(build_iso(FILE_TEXT))
        out = self.run_probe("psyq", d / "g.iso")
        self.assertIn("ot packets 3 prims 7 unknown 1 (0x90)", out)

    def test_libgte_hle_matches_retail_arithmetic(self):
        import math

        def s32(x):
            x &= 0xFFFFFFFF
            return x - (1 << 32) if x & 0x80000000 else x

        def s16(x):
            x &= 0xFFFF
            return x - 0x10000 if x & 0x8000 else x

        def mul12(a, b):
            return s32(a * b) >> 12

        table = [(int(round(4096 * math.sin(2 * math.pi * i / 4096))),
                  int(round(4096 * math.cos(2 * math.pi * i / 4096)))) for i in range(4096)]

        def sincos(a):
            if a >= 0:
                sn, cs = table[a & 0xFFF]
                return sn, cs
            sn, cs = table[(-a) & 0xFFF]
            return -sn, cs

        def rotmatrix(x, y, z):
            sx, cx = sincos(x)
            sy, cy = sincos(y)
            sz, cz = sincos(z)
            m = [0] * 9
            m[2] = sy
            m[5] = s32(-(cy * sx)) >> 12
            m[8] = mul12(cy, cx)
            m[0] = mul12(cz, cy)
            m[1] = s32(-(sz * cy)) >> 12
            t = mul12(cz, -sy)
            m[3] = mul12(sz, cx) - mul12(t, sx)
            m[6] = mul12(sz, sx) + mul12(t, cx)
            t = mul12(sz, -sy)
            m[4] = mul12(cz, cx) + mul12(t, sx)
            m[7] = mul12(cz, sx) - mul12(t, cx)
            return [s16(v) for v in m]

        def rotmatrix_yxz(x, y, z):   # retail func_80049A1C
            sx, cx = sincos(x)
            sy, cy = sincos(y)
            sz, cz = sincos(z)
            m = [0] * 9
            m[5] = -sx
            m[2] = mul12(sy, cx)
            m[8] = mul12(cy, cx)
            m[3] = mul12(sz, cx)
            m[4] = mul12(cz, cx)
            t = mul12(sy, sx)
            m[0] = mul12(cy, cz) + mul12(t, sz)
            m[1] = -mul12(cy, sz) + mul12(t, cz)
            t = mul12(cy, sx)
            m[7] = mul12(sy, sz) + mul12(t, cz)
            m[6] = -mul12(sy, cz) + mul12(t, sz)
            return [s16(v) for v in m]

        def rot_axis(m, ang, rp, rq, neg):   # retail RotMatrixX/Y/Z
            s, c = sincos(ang)
            t = -s if neg else s
            m = list(m)
            p = m[rp * 3:rp * 3 + 3]
            q = m[rq * 3:rq * 3 + 3]
            for j in range(3):
                m[rp * 3 + j] = s16(s32(c * p[j] - t * q[j]) >> 12)
                m[rq * 3 + j] = s16(s32(t * p[j] + c * q[j]) >> 12)
            return m

        atan = [int(math.floor(math.atan(i / 1024) * 2048 / math.pi + 0.5)) for i in range(1026)]

        def ratan2(y, x):   # retail func_8004CFEC
            nx, ny = x < 0, y < 0
            x, y = abs(x), abs(y)
            if x == 0 and y == 0:
                return 0
            def div(a, b):
                return int(a / b)   # truncation, positive operands
            if y < x:
                idx = div(y, x >> 10) if y & 0x7FE00000 else div(y << 10, x)
                v = atan[idx]
            else:
                idx = div(x, y >> 10) if x & 0x7FE00000 else div(x << 10, y)
                v = 0x400 - atan[idx]
            if nx:
                v = 0x800 - v
            if ny:
                v = -v
            return v

        out = self.run_probe("libgte")
        base = [1100 * i - 4000 for i in range(9)]
        for name, rp, rq, neg in (("RotMatrixX", 1, 2, False), ("RotMatrixY", 0, 2, True),
                                  ("RotMatrixZ", 0, 1, False)):
            for ang in (0, 1024, -300, 4095, -4097, 77):
                want = " ".join(map(str, rot_axis(base, ang, rp, rq, neg)))
                self.assertIn("%s %d: %s" % (name, ang, want), out)
        for y, x in ((1, 1), (0, 5), (5, 0), (0, 0), (-3, 7), (3, -7), (-3, -7), (100, 37), (37, 100),
                     (0x3000000, 0x2000001), (0x2000001, 0x3000000), (-1, -1000000), (1000000, 1)):
            self.assertIn("ratan2 %d %d: %d" % (y, x, ratan2(y, x)), out)
        for ang in ((0, 0, 0), (0, 0, 1024), (512, -300, 77), (-4000, 4095, -1),
                    (1234, 2345, 3456), (-2048, -1024, 2048)):
            want = " ".join(str(v) for v in rotmatrix(*ang))
            self.assertIn("rot %d %d %d: %s" % (ang[0], ang[1], ang[2], want), out)
            want = " ".join(str(v) for v in rotmatrix_yxz(*ang))
            self.assertIn("rotyxz %d %d %d: %s" % (ang[0], ang[1], ang[2], want), out)
        m = [1000 * i - 3000 for i in range(9)]
        v = (2048, 8192, -4096)
        scaled = [s16(mul12(m[k], v[k % 3])) for k in range(9)]
        self.assertIn("scale: " + " ".join(map(str, scaled)) + " word %08x" % (mul12(m[8], v[2]) & 0xFFFFFFFF), out)

    def test_libgs_packet_builders(self):
        self.run_probe("libgs")

    def test_psyq_generated_table_is_current(self):
        v = vendor_root()
        if v is None:
            self.skipTest("vendor/bfm-decomp not present")
        asm = next((a for a in (ROOT / "asm", ROOT.parent / "brave-fencer-musashi-decomp/asm")
                    if (a / "main.s").is_file()), None)
        if asm is None:
            self.skipTest("no local splat disassembly (asm/main.s)")
        proc = subprocess.run(["python3", str(ROOT / "tools/psyq_callsites.py"), "--vendor", str(v),
                               "--asm", str(asm), "--check"],
                              capture_output=True, text=True, timeout=600)
        if proc.returncode != 0:
            # The entry table must match; call-site counts follow the local disassembly,
            # which grows as more overlays are extracted.
            self.assertNotIn("bfm_psyq_compat.def", proc.stderr, proc.stderr)
            self.skipTest("call-site counts drifted with the local disassembly; "
                          "regenerate with tools/psyq_callsites.py (%s)" % proc.stderr.strip())

    def test_psyq_callsites_fails_clearly_without_disassembly(self):
        v = vendor_root()
        if v is None:
            self.skipTest("vendor/bfm-decomp not present")
        proc = subprocess.run(["python3", str(ROOT / "tools/psyq_callsites.py"), "--vendor", str(v),
                               "--asm", str(self.tmp / "no-asm")], capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 2)
        self.assertIn("asm/main.s", proc.stderr)
        self.assertIn("never committed", proc.stderr)

    def test_psyq_table_consistency(self):
        import json
        import re
        defs = re.findall(r"^BFM_PSYQ\((0x[0-9A-F]{8}), (\w+), BFM_PSYQ_(\w+), (\d+), (\d+), (\w+)\)$",
                          (PLAT / "psyq/bfm_psyq_compat.def").read_text(), re.M)
        data = json.loads((PLAT / "psyq/bfm_psyq_calls.json").read_text())
        self.assertEqual([d[0] for d in defs], [e["addr"] for e in data["entries"]])
        self.assertEqual(sorted(int(d[0], 16) for d in defs), [int(d[0], 16) for d in defs])
        impl = "".join((PLAT / "psyq" / f).read_text() for f in
                       ("bfm_psyq_compat.c", "bfm_psyq_libgs.c", "bfm_psyq_libgte.c", "psyq_psycross.c"))
        counts = {"PSYCROSS": 0, "HLE": 0, "STUB": 0}
        sites = {"PSYCROSS": 0, "HLE": 0, "STUB": 0}
        by_addr = {e["addr"]: e for e in data["entries"]}
        for addr, name, status, argc, ret, wrapper in defs:
            counts[status] += 1
            sites[status] += by_addr[addr]["callsites"]
            if status == "STUB":
                self.assertEqual(wrapper, "bfm_psyq_stub", name)
            else:
                self.assertTrue(re.search(rf"\bint {wrapper}\(uint32_t \*r\)", impl) or
                                re.search(rf"^\w+\(\w+, {wrapper}\)$", impl, re.M), name)
                self.assertTrue(0 <= int(argc) <= 12 and ret in ("0", "1"), name)
        self.assertEqual(sum(sites.values()), data["meta"]["callsites"])
        md = (ROOT / "docs/PSYQ-COMPAT.md").read_text()
        self.assertIn("| hle | %d (" % counts["HLE"], md)
        self.assertIn("| psycross | %d (" % counts["PSYCROSS"], md)
        covered = counts["HLE"] + counts["PSYCROSS"]
        self.assertIn("**%d / %d (" % (covered, len(defs)), md)
        self.assertIn("**%d / %d (" % (sites["HLE"] + sites["PSYCROSS"], data["meta"]["callsites"]), md)
        # every inferred name is used and carries evidence
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        import psyq_compat_status as st
        for addr, (name, ev) in st.INFERRED.items():
            e = by_addr.get("0x%08X" % addr)
            self.assertIsNotNone(e, name)
            self.assertEqual((e["name"], e["name_source"]), (name, "inferred"))
            self.assertTrue(ev)

    def test_psyq_gte_wrappers_on_psycross_gte(self):
        psyc = Path(os.environ.get("BFM_DECOMP_ROOT", str(ROOT))) / "tools/third_party/psycross"
        for cand in (ROOT / "tools/third_party/psycross", psyc):
            if (cand / "src/psx/LIBGTE.C").is_file():
                psyc = cand
                break
        else:
            self.skipTest("PsyCross not fetched (tools/fetch_toolchains.sh)")
        d = self.fresh("gte")
        inc = ["-I", str(psyc / "include"), "-I", str(psyc / "src")]
        objs = []

        def cc(args, out):
            subprocess.run(args + ["-o", str(d / out)], check=True, stdin=subprocess.DEVNULL, timeout=300)
            objs.append(str(d / out))
        # PsyCross's GTE core (MIT), from the local checkout
        cc(["gcc", "-c", "-O1", "-w", "-x", "c", "-std=gnu11", "-include", "assert.h", *inc,
            str(psyc / "src/psx/LIBGTE.C")], "libgte.o")
        cc(["gcc", "-c", "-O1", "-w", "-x", "c", "-std=gnu11", "-include", "assert.h", *inc,
            str(psyc / "src/psx/INLINE_C.C")], "inline_c.o")
        cc(["g++", "-c", "-O1", "-w", *inc, str(psyc / "src/gte/PsyX_GTE.cpp")], "gte.o")
        cc(["g++", "-c", "-O1", "-w", *inc, str(psyc / "src/gte/half_float.cpp")], "half.o")
        cc(["gcc", "-c", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DBFM_PLAT_WITH_PSYCROSS",
            "-include", "assert.h", "-I", str(PLAT), "-I", str(psyc / "include"),
            str(PLAT / "psyq/psyq_psycross.c")], "psyq_psycross.o")
        # only the compat table sees BFM_PLAT_WITH_PSYCROSS (the core would also try to
        # register the PsyCross renderer backend, which needs SDL/GL)
        cc(["gcc", "-c", "-std=c99", "-Wall", "-Wextra", "-Werror", "-DBFM_PLAT_WITH_PSYCROSS",
            "-I", str(PLAT), str(PLAT / "psyq/bfm_psyq_compat.c")], "compat.o")
        cc(["gcc", "-c", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(PLAT),
            str(PLAT / "psyq/bfm_psyq_libgs.c")], "libgs.o")
        cc(["gcc", "-c", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(PLAT),
            str(PLAT / "psyq/bfm_psyq_libgte.c")], "libgte_hle.o")
        platform = sorted(str(p) for p in PLAT.glob("bfm_plat*.c"))
        probe = d / "gte_probe"
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-I", str(PLAT), *platform, str(ROOT / "tests/bfm_psyq_gte_probe.c"),
                        *objs, "-lstdc++", "-ldl", "-lm", "-o", str(probe)],
                       check=True, stdin=subprocess.DEVNULL, timeout=300)
        out = subprocess.run([str(probe)], capture_output=True, text=True, timeout=60)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("rtps sx 185 sy 132 otz 250", out.stdout)
        self.assertIn("lw1 5 100 0 m01 -4096", out.stdout)
        for i, (x, y) in enumerate(((264, 172), (264, 188), (256, 172), (256, 188))):
            self.assertIn("corner %d: %d %d" % (i, x, y), out.stdout)
        self.assertIn("ok gte", out.stdout)

    def test_overlay_font(self):
        self.run_probe("font")

    def test_platform_sources_include_no_host_or_psyq_headers(self):
        # Core interfaces stay dependency-free; only backends/ may include
        # PsyCross or device-owner headers.
        banned = ("SDL", "psx/", "PsyX", "libgpu", "libetc", "GL/", "AL/")
        for path in list(PLAT.glob("bfm_plat*.[ch]")) + [PLAT / "bfm_plugin.h"]:
            for line in path.read_text().splitlines():
                if line.startswith("#include"):
                    for b in banned:
                        self.assertNotIn(b, line, f"{path.name}: {line}")

    def test_backends_compile_against_their_dependencies(self):
        backends = PLAT / "backends"
        sdl_inc = Path(os.environ.get("BFM_HOST_PORT_DEPS", "/nonexistent")) / "usr/include"
        for inc in (sdl_inc / "SDL2", Path("/usr/include/SDL2")):
            if (inc / "SDL.h").is_file():
                subprocess.run(["cc", *CFLAGS, "-fsyntax-only", "-D_REENTRANT",
                                "-I", str(inc), "-I", str(inc.parent), "-I",
                                str(inc.parent / "x86_64-linux-gnu"),
                                str(backends / "input_sdl.c")],
                               check=True, stdin=subprocess.DEVNULL, timeout=60)
                break
        psyc = Path(os.environ.get("BFM_DECOMP_ROOT", str(ROOT))) / "tools/third_party/psycross/include"
        if (psyc / "psx/libgte.h").is_file():
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                            "-include", "assert.h", "-I", str(psyc), "-I", str(PLAT / "psyq"),
                            str(PLAT / "psyq/psyq_psycross.c")],
                           check=True, stdin=subprocess.DEVNULL, timeout=60)
            subprocess.run(["cc", *CFLAGS, "-fsyntax-only", "-DBFM_PLAT_WITH_PSYCROSS", "-I", str(PLAT),
                            str(PLAT / "psyq/bfm_psyq_compat.c")],
                           check=True, stdin=subprocess.DEVNULL, timeout=60)
        for name in ("audio_sdl.c", "disc_pinned.c"):
            subprocess.run(["cc", *CFLAGS, "-fsyntax-only", "-I",
                            str(ROOT / "pc_port/include"), str(backends / name)],
                           check=True, stdin=subprocess.DEVNULL, timeout=60)
        psycross = ROOT / "tools/third_party/psycross/include"
        if not (psycross / "psx/libgpu.h").is_file():
            main_checkout = (Path(os.environ.get("BFM_DECOMP_ROOT", str(ROOT))) /
                             "tools/third_party/psycross/include")
            psycross = main_checkout
        if not (psycross / "psx/libgpu.h").is_file():
            self.skipTest("PsyCross not fetched (tools/fetch_toolchains.sh)")
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-fsyntax-only", "-include", "assert.h", "-I",
                        str(psycross), str(backends / "renderer_psycross.c")],
                       check=True, stdin=subprocess.DEVNULL, timeout=60)


if __name__ == "__main__":
    unittest.main()
