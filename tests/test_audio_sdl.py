"""Live owned SDL2 playback probe; unavailable devices are explicitly NOT_RUN."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _sdl_compile_flags():
    """Use the configured CMake SDK even when its pkg-config path is unset."""
    prefixes = []
    for cache in (ROOT / "build/menu-takeover/CMakeCache.txt",
                  ROOT / "build/CMakeCache.txt",
                  ROOT / "build/menu-resume/CMakeCache.txt"):
        if not cache.exists():
            continue
        match = re.search(r"^SDL2_DIR:(?:PATH|UNINITIALIZED)=(.+)$", cache.read_text(), re.MULTILINE)
        if match:
            sdl_dir = Path(match.group(1))
            if len(sdl_dir.parents) >= 2:
                prefix = sdl_dir.parents[2]
                if ((prefix / "include/SDL2/SDL.h").exists() and
                        (prefix / "lib/libSDL2.so").exists()):
                    return ["-I", str(prefix / "include/SDL2"),
                            "-L", str(prefix / "lib"),
                            "-Wl,-rpath," + str(prefix / "lib"), "-lSDL2"], "CMake SDK at " + str(prefix)
                prefixes.append(prefix)
    prefixes.extend([
        Path("/home/linuxbrew/.linuxbrew"),
        Path("/usr/local"),
        Path("/usr"),
    ])
    unique_prefixes = []
    for prefix in prefixes:
        if prefix not in unique_prefixes:
            unique_prefixes.append(prefix)

    pkg_config = shutil.which("pkg-config")
    if pkg_config:
        for prefix in unique_prefixes:
            pkg_dir = prefix / "lib/pkgconfig"
            if not (pkg_dir / "sdl2.pc").exists():
                continue
            env = os.environ.copy()
            old_path = env.get("PKG_CONFIG_PATH")
            env["PKG_CONFIG_PATH"] = str(pkg_dir) + (
                os.pathsep + old_path if old_path else ""
            )
            query = subprocess.run([pkg_config, "--cflags", "--libs", "sdl2"],
                                   text=True, capture_output=True, env=env)
            if query.returncode == 0:
                return shlex.split(query.stdout), "pkg-config via " + str(pkg_dir)

    for prefix in unique_prefixes:
        header = prefix / "include/SDL2/SDL.h"
        library = prefix / "lib/libSDL2.so"
        if header.exists() and library.exists():
            return ["-I", str(prefix / "include/SDL2"),
                    "-L", str(prefix / "lib"), "-Wl,-rpath," + str(prefix / "lib"),
                    "-lSDL2"], "CMake SDK at " + str(prefix)
    return None, None


def test_audio_sdl_live_probe():
    flags, source = _sdl_compile_flags()
    if flags is None:
        pytest.skip("SDL2 CMake/pkg-config/header/library metadata unavailable; live audio NOT_RUN")
    run_env = os.environ.copy()
    if not run_env.get("XDG_RUNTIME_DIR"):
        runtime_dir = Path(f"/run/user/{os.getuid()}")
        if (runtime_dir / "pipewire-0").exists():
            run_env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    with tempfile.TemporaryDirectory(prefix="musashi-audio-sdl-") as temp:
        probe = Path(temp) / "audio_sdl_live_probe"
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
            "-I", str(ROOT / "pc_port/include"),
            str(ROOT / "tests/audio_sdl_live_probe.c"),
            str(ROOT / "pc_port/audio_sdl.c"),
            str(ROOT / "pc_port/spu_cd_audio.c"),
            *flags, "-o", str(probe),
        ], check=True, timeout=30)
        result = subprocess.run([str(probe)], text=True, capture_output=True,
                                env=run_env, timeout=10)
        if result.returncode == 77:
            pytest.skip((result.stderr.strip() or "real SDL audio device unavailable") +
                        " [SDK: " + str(source) + "]")
        assert result.returncode == 0, result.stderr + " [SDK: " + str(source) + "]"
