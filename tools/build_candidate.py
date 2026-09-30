#!/usr/bin/env python3
"""Compile one C file with a pinned Psy-Q toolchain and extract a function's bytes.

This produces candidates for match_function.py to judge. It does not decide
anything itself: a successful compile is not evidence of anything, and the
output is only meaningful once the oracle compares it with retail.

The compiler is driven the way the Psy-Q chain drove it, not the way a modern
gcc driver would: cpp and cc1 are invoked separately, then maspsx reproduces
ASPSX macro expansion before GNU as assembles the result. The ASPSX version is
tied to the compiler rather than passed in loosely, because the wrong expansion
silently prevents a match instead of failing loudly.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from retail_common import RetailError

WORD_SIZE = 4
DEFAULT_TOOLCHAIN_ROOT_ENV = "PSYQ_TOOLCHAIN_ROOT"

# maspsx selects its input source with sys.stdin.isatty() and calls
# sys.stdin.readlines() when stdin is not a tty. Every child therefore gets an
# explicit closed stdin: inheriting it would make this harness work
# interactively and hang under CI. The timeout turns any future hang into a
# loud failure instead of a stalled build.
CHILD_TIMEOUT_SECONDS = 120

# Where linked candidates are placed. Only the 256MB region matters: branches
# are PC-relative and %hi/%lo carry absolute targets, but `jal` takes its top
# four address bits from the delay-slot PC, so the base must share a region
# with the retail executable. This is the executable's own text address.
DEFAULT_LINK_BASE = 0x80010000

# splat writes these in linker-assignment syntax, so they can be layered
# straight into the generated script.
DEFAULT_SYMBOL_FILES = (
    "config/undefined_syms.auto.txt",
    "config/undefined_funcs.auto.txt",
    "config/symbol_addrs.txt",
)


@dataclass(frozen=True)
class Toolchain:
    name: str
    aspsx_version: str


# PSY-Q release -> compiler -> ASPSX assembler version.
TOOLCHAINS: dict[str, Toolchain] = {
    "gcc-2.6.0-psx": Toolchain("gcc-2.6.0-psx", "2.34"),
    "gcc-2.7.2-psx": Toolchain("gcc-2.7.2-psx", "2.56"),
    "gcc-2.7.2-cdk-psx": Toolchain("gcc-2.7.2-cdk-psx", "2.67"),
    "gcc-2.8.0-psx": Toolchain("gcc-2.8.0-psx", "2.77"),
    "gcc-2.8.1-psx": Toolchain("gcc-2.8.1-psx", "2.79"),
    "gcc-2.91.66-psx": Toolchain("gcc-2.91.66-psx", "2.81"),
}

TEXT_SYMBOL_TYPES = frozenset("Tt")


def default_toolchain_root(repo: Path) -> Path:
    """Where fetch_toolchains.sh installs the candidate compilers."""

    return repo / "tools" / "psyq"


SYMBOL_DEFINITION_RE = re.compile(
    r"^\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<value>0[xX][0-9a-fA-F]+|\d+)\s*;?(?:\s*//.*|\s*#.*)?$"
)


def parse_symbol_definitions(text: str) -> dict[str, int]:
    """Read `name = 0xADDR;` assignments as splat emits them.

    A later definition overrides an earlier one, so files can be layered in a
    deliberate order. Lines that are blank, commented, or not an assignment are
    skipped rather than treated as an error: these files are machine-generated
    and carry incidental content.
    """

    symbols: dict[str, int] = {}
    for line in text.splitlines():
        match = SYMBOL_DEFINITION_RE.match(line)
        if match is None:
            continue
        symbols[match.group("name")] = int(match.group("value"), 0)
    return symbols


def discard_candidate_definition(definitions: dict[str, int], symbol: str) -> None:
    """Keep the object text symbol authoritative during candidate linking."""

    definitions.pop(symbol, None)


SPLAT_NAME_RE = re.compile(r"^(?:func_|D_)(?P<address>[0-9a-fA-F]{8})$")


def derive_symbol_address(name: str) -> int | None:
    """Recover an address from a splat-generated symbol name.

    splat names unidentified symbols after their address, so `func_80042610`
    lives at 0x80042610. This covers callees that sit inside the split and are
    therefore absent from undefined_funcs.auto.txt while still being undefined
    in a single compiled object.

    Returns None for any name that does not carry an address, so a real
    external dependency is reported rather than silently invented.
    """

    match = SPLAT_NAME_RE.match(name)
    if match is None:
        return None
    return int(match.group("address"), 16)


def parse_undefined_symbols(nm_output: str) -> list[str]:
    """List symbols an object references but does not define, from `nm`."""

    undefined = set()
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] == "U":
            undefined.add(fields[1])
    return sorted(undefined)


def render_linker_script(base: int, symbols: dict[str, int],
                         rodata_base: int | None = None) -> str:
    """Build a script that resolves external references and places .text.

    Assignments must precede SECTIONS or ld cannot resolve references to them.

    Placement must be exact for internal absolute jumps and text addresses.
    Override input-section alignment: a retail function may only be word
    aligned even when GNU as gives its standalone .text 16-byte alignment.
    """

    if rodata_base is not None and (not 0 <= rodata_base <= 0xFFFFFFFC or rodata_base % 4):
        raise RetailError("rodata base must be a word-aligned 32-bit address")
    rodata = (f"    .rodata 0x{rodata_base:08X} : SUBALIGN(4) {{ *(.rdata) *(.rodata*) }}\n"
              if rodata_base is not None else "")
    assignments = "".join(
        f"{name} = 0x{address:08X};\n" for name, address in sorted(symbols.items())
    )
    return (
        "/* Generated by tools/build_candidate.py. Do not edit. */\n"
        f"{assignments}"
        "SECTIONS\n"
        "{\n"
        f"    . = 0x{base:08X};\n"
        f"    .text 0x{base:08X} : SUBALIGN(4) {{ *(.text) }}\n"
        f"{rodata}"
        "    /DISCARD/ : { *(.pdr) *(.comment) *(.reginfo) *(.mdebug*) }\n"
        "}\n"
    )


def default_include_dirs(repo: Path) -> list[Path]:
    """Include directories searched for every decompiled source.

    cpp runs with -nostdinc, so decomp sources cannot use src/shared/types.h;
    it pulls in stdint.h. include/ holds freestanding PSX typedefs instead.
    """

    return [repo / "include"]


def default_maspsx_path(repo: Path) -> Path:
    """Where the vendored maspsx lives.

    Resolved from the repository root rather than from the toolchain root:
    pointing --toolchain-root elsewhere must not move the assembler shim.
    """

    return repo / "tools" / "maspsx" / "maspsx.py"


def resolve_toolchain(name: str) -> Toolchain:
    toolchain = TOOLCHAINS.get(name)
    if toolchain is None:
        available = ", ".join(sorted(TOOLCHAINS))
        raise RetailError(f"unknown toolchain {name!r}; available: {available}")
    return toolchain


def parse_nm_symbols(output: str) -> dict[str, tuple[int, int | None]]:
    """Map text symbol names to (section offset, size) from `nm -S` output.

    Only text symbols are kept, since a function's bytes live in .text. Symbols
    without a recorded size are reported as unsized rather than guessed at.
    """

    symbols: dict[str, tuple[int, int | None]] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4:
            value, size, kind, name = fields
        elif len(fields) == 3:
            value, kind, name = fields
            size = None
        else:
            continue
        if kind not in TEXT_SYMBOL_TYPES:
            continue
        try:
            offset = int(value, 16)
            length = int(size, 16) if size is not None else None
        except ValueError:
            continue
        symbols[name] = (offset, length)
    return symbols


def slice_symbol(section: bytes, offset: int, size: int) -> bytes:
    """Cut a symbol's bytes out of a section image."""

    if size <= 0:
        raise RetailError(f"symbol size must be positive, got {size}")
    if size % WORD_SIZE:
        raise RetailError(f"symbol size {size} is not a multiple of {WORD_SIZE}")
    if offset % WORD_SIZE:
        raise RetailError(f"symbol offset {offset} is not word-aligned")
    if offset + size > len(section):
        raise RetailError(
            f"symbol range {offset}+{size} overruns the {len(section)}-byte section"
        )
    return section[offset : offset + size]


def cpp_command(cpp: Path, source: Path, includes: list[Path] | None = None) -> list[str]:
    command = [
        str(cpp),
        "-undef",
        "-D__GNUC__=2",
        "-D__OPTIMIZE__",
        "-Dmips",
        "-D__mips__",
        "-D__LITTLE_ENDIAN__",
        "-D_LANGUAGE_C",
        "-nostdinc",
    ]
    for include in includes or []:
        command.append(f"-I{include}")
    command.append(str(source))
    return command


def cc1_command(
    cc1: Path, preprocessed: Path, output: Path, *, optimization: str = "-O2", gp: str = "-G0"
) -> list[str]:
    return [
        str(cc1),
        optimization,
        gp,
        "-mips1",
        "-mcpu=3000",
        "-w",
        "-funsigned-char",
        "-fpeephole",
        "-ffunction-cse",
        "-fpcc-struct-return",
        "-fcommon",
        "-msoft-float",
        "-mgas",
        "-fgnu-linker",
        "-quiet",
        str(preprocessed),
        "-o",
        str(output),
    ]


def maspsx_command(
    maspsx: Path,
    assembly: Path,
    obj: Path,
    *,
    aspsx_version: str,
    gnu_as: Path,
    gp: str = "-G0",
    expand_div: bool = True,
) -> list[str]:
    """Build the maspsx invocation.

    Division expansion defaults on because retail requires it: all 69 division
    sites in the executable carry the bnez/nop/break divide-by-zero guard, and
    no candidate toolchain emits that guard without --expand-div. See
    docs/MATCHING.md.
    """

    command = [
        sys.executable,
        str(maspsx),
        f"--aspsx-version={aspsx_version}",
        "--use-comm-section",
        "--run-assembler",
        f"--gnu-as-path={gnu_as}",
    ]
    if expand_div:
        command.append("--expand-div")
    command += [
        "-EL",
        gp,
        "-march=r3000",
        "-mtune=r3000",
        "-no-pad-sections",
        "-Iinclude",
        "-Ivendor/bfm-decomp/include",
        "-o",
        str(obj),
        str(assembly),
    ]
    return command


# Assemblers a registry entry may name. None is the default ASPSX path
# (maspsx). "gnu" is for Sony objects that were assembled with GNU as, which
# fills branch/`jr $ra` delay slots itself and uses the destination register
# rather than $at for an indexed load (docs: StopRCnt 8005D06C).
ASSEMBLERS = ("gnu",)
_SET_VOLATILE_RE = re.compile(r"^\s*#?\s*\.set\s+(?:no)?volatile\b.*$", re.MULTILINE)


_MOVE_RE = re.compile(r"^(\s*)move\s+(\$\w+)\s*,\s*(\$\w+)\s*$", re.MULTILINE)


def strip_set_volatile(assembly_text: str) -> str:
    """Drop the ASPSX-only `.set volatile` / `.set novolatile` markers cc1 emits."""
    return _SET_VOLATILE_RE.sub("", assembly_text)


def gnu_as_source(assembly_text: str) -> str:
    """Prepare cc1 output for modern GNU as the way the period GNU as read it.

    Besides dropping `.set volatile`, `move` is spelled out as
    `addu rd,rs,$0`: the period assembler (and retail) encode it that way,
    while current binutils emit `or rd,rs,$0` for the same pseudo-op.
    """
    return _MOVE_RE.sub(r"\1addu\t\2,\3,$0", strip_set_volatile(assembly_text))


def gnu_as_command(gnu_as: Path, assembly: Path, obj: Path, *, gp: str = "-G0") -> list[str]:
    """Assemble cc1 output directly with GNU as, as the Sony GNU-as objects were."""
    return [
        str(gnu_as),
        "-EL",
        gp,
        "-march=r3000",
        "-O2",
        "-no-pad-sections",
        "-Iinclude",
        "-Ivendor/bfm-decomp/include",
        "-o",
        str(obj),
        str(assembly),
    ]


SECTION_HEADER_RE = re.compile(
    r"^\s*\d+\s+(?P<name>\.\S+)\s+(?P<size>[0-9a-fA-F]+)\s+(?P<vma>[0-9a-fA-F]+)\s"
)


def parse_section_address(objdump_output: str, section: str) -> int:
    """Read a section's actual load address from `objdump -h`.

    The linked image does not necessarily begin at the requested base: ld
    honours the section's alignment, so a base that is not sufficiently aligned
    is rounded up. Slicing relative to the requested base would then be off by
    the padding — and for a function that still fits inside the section, it
    would be off silently.
    """

    for line in objdump_output.splitlines():
        match = SECTION_HEADER_RE.match(line)
        if match and match.group("name") == section:
            return int(match.group("vma"), 16)
    raise RetailError(f"section {section!r} not found in the linked image")


def ld_command(ld: Path, script: Path, obj: Path, elf: Path) -> list[str]:
    """Link one object against the generated script.

    -EL is required: the toolchain's ld defaults to big-endian MIPS and rejects
    a little-endian object outright.
    """

    return [str(ld), "-EL", "-T", str(script), "-o", str(elf), str(obj)]


def _run(command: list[str], label: str, *, stdout: Path | None = None) -> None:
    try:
        if stdout is not None:
            with stdout.open("wb") as stream:
                completed = subprocess.run(
                    command,
                    check=False,
                    stdout=stream,
                    stderr=subprocess.PIPE,
                    stdin=subprocess.DEVNULL,
                    timeout=CHILD_TIMEOUT_SECONDS,
                )
        else:
            completed = subprocess.run(
                command,
                check=False,
                capture_output=True,
                stdin=subprocess.DEVNULL,
                timeout=CHILD_TIMEOUT_SECONDS,
            )
    except subprocess.TimeoutExpired as exc:
        raise RetailError(f"{label} timed out after {CHILD_TIMEOUT_SECONDS}s") from exc
    except OSError as exc:
        raise RetailError(f"failed to execute {label}: {exc}") from exc
    if completed.returncode != 0:
        detail = (completed.stderr or b"").decode("utf-8", errors="replace").strip()
        raise RetailError(f"{label} failed with exit code {completed.returncode}: {detail}")


def _capture(command: list[str], label: str) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            errors="replace",
            stdin=subprocess.DEVNULL,
            timeout=CHILD_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as exc:
        raise RetailError(f"{label} timed out after {CHILD_TIMEOUT_SECONDS}s") from exc
    except OSError as exc:
        raise RetailError(f"failed to execute {label}: {exc}") from exc
    if completed.returncode != 0:
        raise RetailError(f"{label} failed with exit code {completed.returncode}: {completed.stderr.strip()}")
    return completed.stdout


def _require(path: Path, what: str) -> Path:
    if not path.is_file():
        raise RetailError(f"{what} not found: {path}")
    return path


def _resolve_binutil(name: str) -> Path:
    found = shutil.which(name)
    if found is None:
        raise RetailError(f"required binutil {name!r} was not found on PATH")
    return Path(found)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="C file to compile")
    parser.add_argument("--symbol", required=True, help="function symbol to extract")
    parser.add_argument("--toolchain", default="gcc-2.7.2-psx", help="pinned Psy-Q toolchain name")
    parser.add_argument(
        "--toolchain-root",
        type=Path,
        help=f"directory holding toolchain subdirectories (or ${DEFAULT_TOOLCHAIN_ROOT_ENV})",
    )
    parser.add_argument("--maspsx", type=Path, help="path to maspsx.py")
    parser.add_argument("--assembler", choices=ASSEMBLERS,
                        help="assemble with GNU as instead of maspsx/ASPSX (default: ASPSX)")
    parser.add_argument("--output", type=Path, required=True, help="where to write the extracted bytes")
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--gp", default="-G0")
    parser.add_argument("--include", type=Path, action="append", default=[])
    parser.add_argument(
        "--link-base",
        type=lambda value: int(value, 0),
        default=DEFAULT_LINK_BASE,
        help="address to place .text at; must share a 256MB region with retail",
    )
    parser.add_argument(
        "--symbols",
        type=Path,
        action="append",
        dest="symbol_files",
        help="symbol definition file, repeatable; defaults to splat's auto files",
    )
    parser.add_argument("--keep-intermediates", action="store_true")
    parser.add_argument("--rodata-base", type=lambda value: int(value, 0),
                        help="explicit read-only data address for switch-table recovery; "
                             "does not verify the table or register a match")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    # Clear any earlier candidate first. match_function compares whatever file
    # it is handed, so a stale artifact surviving a failed build would be
    # silently reported on as if it were current.
    args.output.unlink(missing_ok=True)
    try:
        repo = Path(__file__).resolve().parents[1]
        toolchain = resolve_toolchain(args.toolchain)
        env_root = os.environ.get(DEFAULT_TOOLCHAIN_ROOT_ENV)
        root = args.toolchain_root or (Path(env_root) if env_root else default_toolchain_root(repo))
        toolchain_dir = Path(root).expanduser() / toolchain.name
        cpp = _require(toolchain_dir / "cpp", f"cpp for {toolchain.name} (run tools/fetch_toolchains.sh)")
        cc1 = _require(toolchain_dir / "cc1", f"cc1 for {toolchain.name} (run tools/fetch_toolchains.sh)")
        maspsx = None
        if args.assembler is None:
            maspsx = _require(
                args.maspsx or default_maspsx_path(repo), "maspsx.py (run tools/fetch_toolchains.sh)"
            )
        gnu_as = _resolve_binutil("mips-linux-gnu-as")
        ld = _resolve_binutil("mips-linux-gnu-ld")
        nm = _resolve_binutil("mips-linux-gnu-nm")
        objcopy = _resolve_binutil("mips-linux-gnu-objcopy")
        objdump = _resolve_binutil("mips-linux-gnu-objdump")
        source = _require(args.source, "source")
        # Missing symbol files are tolerated: a function with no external
        # references links without them. One that does have references fails
        # loudly at ld instead, which is the right place to notice.
        symbol_files = args.symbol_files or [repo / name for name in DEFAULT_SYMBOL_FILES]

        workspace = Path(tempfile.mkdtemp(prefix="candidate."))
        try:
            preprocessed = workspace / "unit.i"
            assembly = workspace / "unit.s"
            obj = workspace / "unit.o"
            text = workspace / "unit.text.bin"

            includes = default_include_dirs(repo) + list(args.include)
            _run(cpp_command(cpp, source, includes), "cpp", stdout=preprocessed)
            _run(
                cc1_command(cc1, preprocessed, assembly, optimization=args.optimization, gp=args.gp),
                "cc1",
            )
            if args.assembler == "gnu":
                assembly.write_text(gnu_as_source(assembly.read_text(encoding="utf-8")),
                                    encoding="utf-8")
                _run(gnu_as_command(gnu_as, assembly, obj, gp=args.gp), "as")
            else:
                _run(
                    maspsx_command(
                        maspsx,
                        assembly,
                        obj,
                        aspsx_version=toolchain.aspsx_version,
                        gnu_as=gnu_as,
                        gp=args.gp,
                    ),
                    "maspsx",
                )

            # Link before extracting. Bytes taken straight from the object still
            # carry unresolved relocations wherever the function references a
            # global or calls another function, and those cannot be compared
            # with retail, which is fully linked.
            elf = workspace / "unit.elf"
            script = workspace / "unit.ld"
            definitions: dict[str, int] = {}
            for path in symbol_files:
                if path.is_file():
                    definitions.update(parse_symbol_definitions(path.read_text(encoding="utf-8")))

            # The generated address table can contain the function currently
            # being built. Its absolute assignment would override the object
            # symbol and make ld report the candidate as type A instead of
            # text, so remove only this symbol and let the object define it.
            discard_candidate_definition(definitions, args.symbol)

            # Anything still undefined that carries its address in its name is
            # resolved from the name. A name without one is a genuine external
            # dependency and is reported rather than guessed at.
            undefined = parse_undefined_symbols(_capture([str(nm), "-u", str(obj)], "nm -u"))
            unresolved = []
            for name in undefined:
                if name in definitions:
                    continue
                address = derive_symbol_address(name)
                if address is None:
                    unresolved.append(name)
                else:
                    definitions[name] = address
            if unresolved:
                raise RetailError(
                    "cannot resolve external symbols with no address in their name: "
                    + ", ".join(unresolved)
                )

            script.write_text(render_linker_script(args.link_base, definitions,
                                                   args.rodata_base), encoding="utf-8")
            _run(ld_command(ld, script, obj, elf), "ld")
            if args.rodata_base is not None:
                actual = parse_section_address(
                    _capture([str(objdump), "-h", str(elf)], "objdump -h"), ".rodata")
                if actual != args.rodata_base:
                    raise RetailError("linked read-only data address differs from requested base")

            symbols = parse_nm_symbols(_capture([str(nm), "-S", str(elf)], "nm"))
            if args.symbol not in symbols:
                known = ", ".join(sorted(symbols)) or "(none)"
                raise RetailError(f"symbol {args.symbol!r} not found in {elf.name}; text symbols: {known}")
            address, size = symbols[args.symbol]
            if size is None:
                raise RetailError(
                    f"symbol {args.symbol!r} has no recorded size; cannot bound its bytes"
                )

            _run(
                [str(objcopy), "-O", "binary", "--only-section=.text", str(elf), str(text)],
                "objcopy",
            )
            # nm reports absolute addresses after linking. The image starts at
            # .text's actual address, which is not necessarily the requested
            # base: ld rounds up to the section's alignment.
            text_address = parse_section_address(
                _capture([str(objdump), "-h", str(elf)], "objdump -h"), ".text"
            )
            extracted = slice_symbol(text.read_bytes(), address - text_address, size)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(extracted)

            print(
                f"CANDIDATE symbol={args.symbol} toolchain={toolchain.name} "
                f"aspsx={toolchain.aspsx_version} {args.optimization} {args.gp}"
            )
            print(f"  size=0x{size:X} words={size // WORD_SIZE} output={args.output}")
            if args.keep_intermediates:
                print(f"  intermediates={workspace}")
            return 0
        finally:
            if not args.keep_intermediates:
                shutil.rmtree(workspace, ignore_errors=True)
    except RetailError as exc:
        print(f"build_candidate: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
