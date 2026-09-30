# Snapshot scans

Every scan was run over the exact committed tree, and the retail references
stayed on the owner's machine. Reference paths are shown relative to the
local extraction directory (`extracted/`), and BIOS images as `<BIOS>/`.

**Result: 0 retail data runs, 0 Psy-Q/Sony code hits, 0 secrets or personal
data.**

## References used

- The boot executable `extracted/disc/files/SLUS_007.26`, and every file
  `dumpsxiso` extracted from the disc (`extracted/disc/files/`: the nine
  `.CD` archives, `.STR` video, `.WAV`, `SYSTEM.CNF`, `MUSA.ID`,
  `license_data.dat`).
- The raw disc image `extracted/disc/disc.bin`.
- Every locally decoded member: 871 files under `extracted/overlays/`,
  `extracted/pac/`, `extracted/sc01/` and `extracted/models/`. These are the
  LZSS-decoded code and data that do not appear verbatim in the `.CD`
  archives.
- BIOS: a search of the home directory for `scph*.bin` and `openbios*`
  found one retail BIOS image, SCPH-5500
  (sha256 `11052b64…1fef`), and the open-source OpenBIOS build. Both were
  used.
- 779 unique reference files in total (by SHA-256), 1,082,808,909 bytes.

## 1. Project retail guard (`tools/retail_guard.py`, `--run-bytes 32`)

This scan checks file names (disc images, boot executables, saves), whole-file
SHA-256 identity with every extracted file, and runs of 32 or more
consecutive bytes. The default is 64, so this setting is stricter. The guard
ignores runs of low-entropy padding.

```
$ python3 tools/retail_guard.py --stage . --exe extracted/disc/files/SLUS_007.26 \
    --extracted extracted/disc/files --extracted extracted/overlays \
    --extracted extracted/pac --extracted extracted/sc01 \
    --run-source <each .CD, MUSA.ID, SYSTEM.CNF, license_data.dat> \
    --run-source <BIOS>/scph5500.bin --run-source <BIOS>/openbios.bin --run-bytes 32
retail_guard: 918 files checked, 0 problem(s)
exit status: 0

$ python3 tools/retail_guard.py --stage . --exe extracted/disc/files/SLUS_007.26 \
    --run-source <each of the 871 decoded overlay/PAC/SC01/model files> --run-bytes 32
retail_guard: 918 files checked, 0 problem(s)
exit status: 0
```

## 2. Independent byte-run and text-encoding scan

This scanner was written for the snapshot. It checks every file as raw bytes,
and also decodes each text file into several numeric encodings:

- 8-hex-digit words, little and big endian (`w32le`/`w32be`);
- `0xNNNN` halfwords (`h16le`);
- `0xNN` and `\xNN` byte tokens (`b8`);
- bare hex strings (`hexstr`);
- decimal byte, int16 and int32 lists (`dec8`/`dec16le`/`dec32le`).

Each decoded stream is compared with every reference. Every staged stream is
indexed at every offset in 16-byte windows, and the references are probed at
16-byte-aligned offsets. So every common run of 31 bytes or more is found,
then extended byte-exactly.

A run fails the scan at these lengths:

- 32 bytes for raw bytes and the byte, halfword and hex-string encodings;
- 64 bytes (16 words) for the word encodings.

Word runs of 32 bytes or more are also reported. Runs that are only an
arithmetic progression (for example the register numbers 0, 1, 2, ..., 15
matching an identity table) are classified as not data.

```
reference files (unique by sha256): 779; reference bytes: 1082808909
arithmetic-progression matches (not data; e.g. 0,1,...,15): 24
  seq  docs/MODDING.md [dec16le @83] 34 bytes == extracted/disc/disc.bin @0x6ea06
  seq  docs/MODDING.md [dec16le @86] 32 bytes == extracted/overlays/sc01/0000.pac0.bin @0x6ca6c
  seq  docs/MODDING.md [dec32le @180] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/mips_formatter.c [dec32le @38000] 36 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/native_boot.c [dec32le @3084] 32 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/platform/backends/renderer_gl.c [dec32le @113] 35 bytes == <BIOS>/scph5500.bin @0x4cf7d
  seq  pc_port/platform/bfm_plat_font.c [dec32le @8] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/platform/bfm_plat_sha256.c [dec32le @124] 32 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/platform/bfm_plat_sha256.c [dec32le @188] 36 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/platform/bfm_plugin.h [dec16le @3] 32 bytes == extracted/disc/disc.bin @0x6ea08
  seq  pc_port/platform/bfm_plugin.h [dec16le @4] 32 bytes == extracted/overlays/sc01/0000.pac0.bin @0x6ca6c
  seq  pc_port/platform/bfm_plugin.h [dec32le @8] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  pc_port/spu_cd_audio.c [dec32le @549] 39 bytes == extracted/overlays/sc03/0030.pac0.bin @0x61969
  seq  tests/bfm_plat_probe.c [dec32le @241] 35 bytes == extracted/disc/disc.bin @0x76df1
  seq  tests/cd_owned_probe.c [dec32le @981] 35 bytes == extracted/disc/disc.bin @0x76df1
  seq  tests/heap_continuation_probe.c [dec32le @132] 32 bytes == extracted/disc/disc.bin @0x76df0
  seq  tests/test_retail_tools.py [dec32le @84] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  tools/identify_retail.py [dec32le @40] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  tools/psyq_callsites.py [dec16le @95] 32 bytes == extracted/disc/disc.bin @0x6ea08
  seq  tools/psyq_callsites.py [dec16le @96] 32 bytes == extracted/overlays/sc01/0000.pac0.bin @0x6ca6c
  seq  tools/psyq_callsites.py [dec32le @192] 40 bytes == extracted/disc/disc.bin @0x76df0
  seq  tools/retail_level1_observe.lua [dec16le @27] 32 bytes == extracted/disc/disc.bin @0x6ea08
  seq  tools/retail_level1_observe.lua [dec16le @28] 32 bytes == extracted/overlays/sc01/0000.pac0.bin @0x6ca6c
  seq  tools/retail_level1_observe.lua [dec32le @56] 40 bytes == extracted/disc/disc.bin @0x76df0
FAIL-level runs: 0
report-only runs (below fail threshold): 3
  info pc_port/sio_controller.c [dec32le @52] 32 bytes == <BIOS>/openbios.bin @0x1db64
  info tests/irq_scheduler_probe.c [dec32le @348] 36 bytes == <BIOS>/openbios.bin @0x1db64
  info tests/test_gpu_controller.py [dec32le @341] 44 bytes == extracted/disc/files/SC02.CD @0x1389629
exit status: 0
```

The report-only lines are decimal lists of small integers (register, slot and
count values in port code and tests) that happen to line up with 8 to 11
words of the references. They are below the
16-word threshold and are not game data.

Found and fixed during review (files excluded before this run):

- `tests/test_bios_heap.py`: 40 BIOS bytes as a hex string.
- `tests/spu_reverb_probe.c` and `tests/spu_reverb_source_probe.c`: a
  64-byte SPU reverb preset table that appears verbatim in the executable.
- `docs/MILESTONE-1.md`: a quoted 55-byte executable header string. This was
  scrubbed rather than excluded.

## 3. Psy-Q / Sony and 4. secrets / personal-data scan

The Psy-Q scan looks for:

- Sony copyright strings;
- includes of Sony SDK headers (`LIBGTE.H` and the like);
- origin markers (`psyq-lib from`, `hand-decompiled`, `Sony libXX`);
- definitions of about 150 Psy-Q API function names;
- any `func_XXXXXXXX` body whose address falls in one of the 71 Sony library
  ranges of `config/psyq_ranges.txt`.

The secrets scan uses gitleaks-style patterns (GitHub, OpenAI/Anthropic, AWS,
Google and Slack tokens, private-key blocks, bearer tokens, password
assignments). The personal-data scan looks for emails, IPv4 addresses,
home-directory paths, local hostnames and names, `~/` local paths, and agent
scratch paths.

```
$ python3 scan_text.py . SCANS.md    # SCANS.md is skipped here because it quotes this output
== Psy-Q / Sony marker scan ==
sony_copyright: 0
sdk_header_include: 0
psyq_origin_marker: 0
sdk_function_definition: 0
sony_copyright (documentation): 3
    NOTICE.md:9: Sony Interactive
    NOTICE.md:10: Sony Interactive
    NOTICE.md:11: Sony Interactive
sdk_header_include (documentation): 0
psyq_origin_marker (documentation): 6
    EXCLUDED.md:36: psyq-lib from
    EXCLUDED.md:36: psyq-lib from
    EXCLUDED.md:36: hand-decompiled
    EXCLUDED.md:36: Sony libgs
    EXCLUDED.md:37: Sony libgte
    docs/KNOWN-DIVERGENCES.md:102: Sony libpress
psyq_range_function_definition (func_XXXXXXXX bodies inside 71 Psy-Q ranges): 0
reviewed (not SDK code): 1
    tests/console_probe.c: func_8005CF38 in libapi1: 4-line capture stub standing in for the library write call so the probe can observe the game's console formatter; written for the test, contains no SDK code
address-named func_ definitions in staged C/H (any range): 2
    pc_port/main.c: func_8001311C
    tests/console_probe.c: func_8005CF38
documentation files mentioning Psy-Q/Sony (by name, count of mentions): 14 files, 191 mentions

== secrets / personal-data scan ==
github_token: 0
openai_anthropic_key: 0
aws_key: 0
google_api_key: 0
slack_token: 0
private_key_block: 0
bearer_token: 0
password_assignment: 0
email: 0
ipv4: 0
home_path: 15
    CMakeLists.txt:33: /home/linuxbrew
    CMakeLists.txt:34: /home/linuxbrew
    CMakeLists.txt:34: /home/linuxbrew
    docs/MENU-BOOT-CONTINUATION.md:606: /home/linuxbrew
    docs/MENU-BOOT-CONTINUATION.md:607: /home/linuxbrew
    docs/PC-PORT.md:890: /home/linuxbrew
    tests/conftest.py:4: /home/linuxbrew
    tests/conftest.py:20: /home/linuxbrew
    tests/conftest.py:21: /home/linuxbrew
    tests/test_audio_sdl.py:35: /home/linuxbrew
    tests/test_list_irq.py:60: /home/linuxbrew
    tests/test_native_smoke.py:25: /home/linuxbrew
    tests/test_native_smoke.py:37: /home/linuxbrew
    tools/run_tests.sh:21: /home/linuxbrew
    tools/run_tests.sh:22: /home/linuxbrew
personal_names_hosts: 0
tilde_local_paths: 6
    CMakeLists.txt:36: ~/opt
    docs/PC-PORT.md:878: ~/opt
    pc_port/native_boot.c:3284: ~/.cache
    pc_port/native_boot.c:4083: ~/.cache
    tools/gl_bench/run.sh:4: ~/opt
    tools/run_headless_boot.sh:40: ~/opt
agent_scratch_paths: 3
    .gitignore:102: .claude/
    EXCLUDED.md:92: codex-session
    EXCLUDED.md:92: .claude/
exit status: 0
```

SCANS.md itself was then scanned on its own as part of the full tree. Its only
hits are the quotations above.

How the remaining lines were reviewed:

- `(documentation)` lines are ownership statements in NOTICE.md and the
  exclusion list in EXCLUDED.md.
- The one `reviewed` line is a four-line test double written for the test.
- `home_path` hits are `/home/linuxbrew`, the standard Homebrew-on-Linux
  prefix.
- `tilde_local_paths` hits are generic, documented defaults
  (`~/opt/host-port-deps`, `~/.cache/bfm-port`).
- `agent_scratch_paths` hits are the `.gitignore` entry and the exclusion
  list, which keep those paths out.

None of these is a secret or personal data.

## Address-only content (allowed)

Some files hold facts, not data: addresses, sizes, offsets and digests, the
same facts a symbol map records. They are:

- the splat and symbol files in `config/` (addresses, sizes, names and
  SHA-1s);
- `config/psyq_ranges.txt` (library address ranges);
- `tests/data/formatter_word_ranges.txt` (address and word-count pairs);
- `tests/data/retail_asm_tiles_allowlist.txt` (source paths);
- `provenance/manifest.json` (CHD size and SHA-256);
- SHA-256 digests of retail spans in port code and tests, and guest RAM
  addresses throughout `pc_port/`.

None of them matched a retail run above.

`pc_port/spu_voice_core.h` and `pc_port/spu_reverb_core.h` carry the SPU
Gaussian interpolation table and the reverb FIR taps as documented by
PSX-SPX. These are hardware constants and do not match the disc or the BIOS.
