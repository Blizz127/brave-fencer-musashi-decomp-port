"""Test-runner environment shims.

Homebrew can leave the *unversioned* SDL2 include tree without `SDL_main.h`
(`/home/linuxbrew/.linuxbrew/include/SDL2`), while a versioned `sdl2-compat`
keg keeps a complete copy. `tools/run_tests.sh` already prefers the versioned
CMake config for the native smoke build, but the compiled probes resolve their
include flags from `pkg-config`/`sdl2-config`, which point at the incomplete
tree. That mismatch made every probe including `<SDL.h>` fail to compile with
`fatal error: SDL_main.h: No such file or directory` — not a code failure.

Prepend the complete include directory to `CPATH` so the child compilers find
it after the (still first-searched) unversioned directory. This is a no-op on a
host whose unversioned tree is complete or whose keg is absent.
"""
from __future__ import annotations

import os
from pathlib import Path

_UNVERSIONED = Path("/home/linuxbrew/.linuxbrew/include/SDL2")
_CELLAR = Path("/home/linuxbrew/.linuxbrew/Cellar")


def _prefer_complete_sdl_include() -> None:
    if (_UNVERSIONED / "SDL_main.h").is_file():
        return
    for candidate in sorted(_CELLAR.glob("sdl2*/**/include/SDL2")):
        if (candidate / "SDL_main.h").is_file():
            os.environ["CPATH"] = (
                str(candidate) + os.pathsep + os.environ.get("CPATH", "")
            )
            return


_prefer_complete_sdl_include()


def _prefer_host_port_deps() -> None:
    deps = os.environ.get("BFM_HOST_PORT_DEPS") or "/nonexistent"
    dep_inc = Path(deps) / "usr/include"
    dep_lib = Path(deps) / "usr/lib/x86_64-linux-gnu"
    if dep_inc.is_dir():
        os.environ["CPATH"] = (
            str(dep_inc) + os.pathsep +
            str(dep_inc / "x86_64-linux-gnu") + os.pathsep +
            os.environ.get("CPATH", "")
        )
    if dep_lib.is_dir():
        pulse_lib = dep_lib / "pulseaudio"
        ld_parts = [str(dep_lib)]
        if pulse_lib.is_dir():
            ld_parts.append(str(pulse_lib))
        os.environ["LIBRARY_PATH"] = (
            str(dep_lib) + os.pathsep +
            os.environ.get("LIBRARY_PATH", "")
        )
        os.environ["LD_LIBRARY_PATH"] = (
            os.pathsep.join(ld_parts) + os.pathsep +
            os.environ.get("LD_LIBRARY_PATH", "")
        )
    tmp_env = os.environ.get("BFM_TEST_TMPDIR")
    if not tmp_env:
        return
    tmp_dir = Path(tmp_env)
    tmp_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("TMPDIR", str(tmp_dir))
    import tempfile
    tempfile.tempdir = str(tmp_dir)


_prefer_host_port_deps()


def _prefer_local_code_image() -> None:
    """Synthetic-RAM probes run main-executable words from the developer's own
    extracted EXE (see MUSASHI_CODE_IMAGE in pc_port/mips_formatter.c); the
    port and the repo carry none. tools/run_tests.sh sets the same default."""
    exe = Path(__file__).resolve().parents[1] / "extracted/disc/files/SLUS_007.26"
    if not os.environ.get("MUSASHI_CODE_IMAGE") and exe.is_file():
        os.environ["MUSASHI_CODE_IMAGE"] = str(exe)


_prefer_local_code_image()

try:
    import pytest

    @pytest.fixture(autouse=True)
    def _pristine_code_image():
        """A mutation test's code image never leaks into the next test."""
        saved = {k: os.environ.get(k) for k in ("MUSASHI_CODE_IMAGE", "MUSASHI_CODE_IMAGE_MUTANT", "MUSASHI_OVERLAY_DIR")}
        yield
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
except ImportError:  # unittest-only runs
    pass
