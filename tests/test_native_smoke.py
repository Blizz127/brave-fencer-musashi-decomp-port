from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _cmake_prefix() -> Path | None:
    """Mirror tools/run_tests.sh: SDL2 and OpenAL come from Homebrew when present.

    Without this prefix, FindOpenAL does not search the Homebrew tree and the
    configure step fails with "Could NOT find OpenAL" even though the keg is
    installed. MUSASHI_CMAKE_PREFIX overrides; an unset variable with no
    Homebrew tree leaves CMake on the system search path alone.
    """
    override = os.environ.get("MUSASHI_CMAKE_PREFIX")
    if override:
        return Path(override)
    brew = Path("/home/linuxbrew/.linuxbrew")
    return brew if brew.is_dir() else None


def _versioned_sdl2_dir() -> Path | None:
    """Prefer the complete sdl2-compat keg over Homebrew's unversioned config.

    The unversioned SDL2 include tree can be missing SDL_main.h; the versioned
    keg's CMake config is complete. tools/run_tests.sh passes the same cache
    entry explicitly.
    """
    for candidate in sorted(
            Path("/home/linuxbrew/.linuxbrew/Cellar").glob("sdl2-compat/*/lib/cmake/SDL2")):
        if (candidate / "SDL2Config.cmake").is_file():
            return candidate
    return None


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ctest"), "CMake/CTest unavailable")
class NativeSmokeTests(unittest.TestCase):
    def test_linux_native_smoke_uses_shared_decomp_source(self) -> None:
        self.assertIn("src/shared/runtime_api.c", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="musashi-native-test-") as temp:
            build = Path(temp) / "build"
            cmake_args = [
                "cmake", "-S", str(ROOT), "-B", str(build), "-DMUSASHI_BUILD_PC_PORT=ON",
            ]
            prefix = _cmake_prefix()
            if prefix is not None:
                cmake_args.append(f"-DCMAKE_PREFIX_PATH={prefix}")
            # A CMAKE_PREFIX_PATH search would otherwise resolve SDL2's include
            # directory to Homebrew's incomplete unversioned tree, so the
            # versioned config follows it as a cache entry that cannot be
            # replaced. tools/run_tests.sh orders these the same way.
            sdl2_dir = _versioned_sdl2_dir()
            if sdl2_dir is not None:
                cmake_args.append(f"-DSDL2_DIR={sdl2_dir}")
            subprocess.run(
                cmake_args,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            subprocess.run(
                ["cmake", "--build", str(build)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            subprocess.run(
                ["ctest", "--test-dir", str(build), "--output-on-failure"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            completed = subprocess.run(
                [str(build / "musashi_pc_smoke")],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            self.assertIn("platform=linux", completed.stdout)
            self.assertIn("le32=0x78563412", completed.stdout)


if __name__ == "__main__":
    unittest.main()
