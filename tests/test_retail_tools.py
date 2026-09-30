from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import extract_retail  # noqa: E402
import identify_retail  # noqa: E402
import register_retail  # noqa: E402
import retail_common  # noqa: E402


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def synthetic_exe(path: Path, fields: tuple[int, ...] | None = None) -> tuple[int, ...]:
    if fields is None:
        fields = (
            0x80010100,
            0x80070000,
            0x80010000,
            0x100,
            0x80060000,
            0x20,
            0x80061000,
            0x80,
            0x801FFF00,
            0x100,
        )
    header = bytearray(retail_common.PSX_EXE_HEADER_SIZE)
    header[: len(retail_common.PSX_EXE_MAGIC)] = retail_common.PSX_EXE_MAGIC
    struct.pack_into("<10I", header, 0x10, *fields)
    path.write_bytes(header + bytes(fields[3]))
    return fields


class RetailCommonTests(unittest.TestCase):
    def test_load_json_requires_object_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.json"
            write_json(path, [])
            with self.assertRaisesRegex(retail_common.RetailError, "root must be an object"):
                retail_common.load_json(path)

    def test_verify_chd_accepts_configurable_synthetic_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture.chd"
            data = b"synthetic-not-retail"
            path.write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            self.assertEqual(
                retail_common.verify_chd(path, expected_size=len(data), expected_sha256=digest),
                digest,
            )

    def test_verify_chd_rejects_missing_size_and_hash_mismatches(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            missing = root / "missing.chd"
            with self.assertRaisesRegex(retail_common.RetailError, "does not exist"):
                retail_common.verify_chd(missing, expected_size=1, expected_sha256="0" * 64)

            path = root / "fixture.chd"
            path.write_bytes(b"abc")
            with self.assertRaisesRegex(retail_common.RetailError, "size mismatch"):
                retail_common.verify_chd(path, expected_size=4, expected_sha256="0" * 64)
            with self.assertRaisesRegex(retail_common.RetailError, "SHA-256 mismatch"):
                retail_common.verify_chd(path, expected_size=3, expected_sha256="0" * 64)

    def test_register_failure_does_not_mutate_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "manifest.json"
            write_json(
                manifest,
                {
                    "media": {
                        "size_bytes": retail_common.EXPECTED_SIZE,
                        "sha256": retail_common.EXPECTED_SHA256,
                    }
                },
            )
            before = manifest.read_bytes()
            bad_chd = root / "bad.chd"
            bad_chd.write_bytes(b"not-the-retail-disc")
            result = register_retail.main([str(bad_chd), "--manifest", str(manifest)])
            self.assertEqual(result, 2)
            self.assertEqual(manifest.read_bytes(), before)


class RepoRelativeTests(unittest.TestCase):
    """Committed provenance must not hard-code one machine's filesystem.

    Paths inside the repository are recorded relative to it so a fresh clone can
    read the manifest. Paths outside stay absolute, because which chdman binary
    ran is genuine provenance that a relative path would lose.
    """

    def test_path_inside_the_repo_is_recorded_relative(self) -> None:
        root = Path("/somewhere/bfm")
        self.assertEqual(
            extract_retail.repo_relative(root / "extracted" / "disc", root),
            "extracted/disc",
        )

    def test_path_outside_the_repo_stays_absolute(self) -> None:
        root = Path("/somewhere/bfm")
        self.assertEqual(
            extract_retail.repo_relative(Path("/usr/bin/chdman"), root), "/usr/bin/chdman"
        )

    def test_the_repo_root_itself_is_recorded_as_dot(self) -> None:
        root = Path("/somewhere/bfm")
        self.assertEqual(extract_retail.repo_relative(root, root), ".")


class ExtractionTests(unittest.TestCase):
    def _manifest(self, root: Path) -> Path:
        path = root / "manifest.json"
        write_json(path, {"media": {"source_path": str(root / "fixture.chd")}})
        return path

    def test_missing_chdman_stops_before_output_writes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = self._manifest(root)
            output = root / "must-not-exist"
            with mock.patch.object(extract_retail, "verify_chd", return_value="a" * 64), mock.patch.object(
                extract_retail.shutil, "which", return_value=None
            ):
                result = extract_retail.main(
                    [str(root / "fixture.chd"), "--manifest", str(manifest), "--output", str(output)]
                )
            self.assertEqual(result, 2)
            self.assertFalse(output.exists())

    def test_dry_run_prints_pipeline_without_output_writes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = self._manifest(root)
            output = root / "must-not-exist"

            def which(name: str) -> str:
                return f"/synthetic/{name}"

            with mock.patch.object(extract_retail, "verify_chd", return_value="a" * 64), mock.patch.object(
                extract_retail.shutil, "which", side_effect=which
            ):
                result = extract_retail.main(
                    [
                        str(root / "fixture.chd"),
                        "--manifest",
                        str(manifest),
                        "--output",
                        str(output),
                        "--dry-run",
                    ]
                )
            self.assertEqual(result, 0)
            self.assertFalse(output.exists())


class IdentityTests(unittest.TestCase):
    def test_system_cnf_and_case_insensitive_boot_discovery(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            payload = Path(temp)
            system_cnf = payload / "SYSTEM.CNF"
            system_cnf.write_text("BOOT = cdrom:\\DATA\\SLUS_999.99;1\r\n", encoding="ascii")
            exe = payload / "files" / "track01" / "data" / "slus_999.99"
            exe.parent.mkdir(parents=True)
            exe.write_bytes(b"x")
            target = identify_retail.read_boot_target(system_cnf)
            self.assertEqual(target, "data\\slus_999.99")
            self.assertEqual(identify_retail.find_boot_exe(payload, target), exe)

    def test_psx_exe_header_uses_official_field_offsets(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / "BOOT.EXE"
            fields = synthetic_exe(exe)
            parsed = identify_retail.parse_psx_exe(exe)
            expected = {
                "initial_pc": fields[0],
                "initial_gp": fields[1],
                "text_address": fields[2],
                "text_size_bytes": fields[3],
                "data_address": fields[4],
                "data_size_bytes": fields[5],
                "bss_address": fields[6],
                "bss_size_bytes": fields[7],
                "initial_sp_base": fields[8],
                "initial_sp_offset": fields[9],
            }
            for key, value in expected.items():
                self.assertEqual(parsed[key], value, key)

    def test_psx_exe_rejects_short_bad_magic_and_oversized_text(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            short = root / "short.exe"
            short.write_bytes(b"PS-X EXE")
            with self.assertRaisesRegex(retail_common.RetailError, "shorter"):
                identify_retail.parse_psx_exe(short)

            bad = root / "bad.exe"
            bad.write_bytes(bytes(retail_common.PSX_EXE_HEADER_SIZE))
            with self.assertRaisesRegex(retail_common.RetailError, "missing"):
                identify_retail.parse_psx_exe(bad)

            oversized = root / "oversized.exe"
            fields = (1, 2, 3, 0x1000, 5, 6, 7, 8, 9, 10)
            header = bytearray(retail_common.PSX_EXE_HEADER_SIZE)
            header[:8] = retail_common.PSX_EXE_MAGIC
            struct.pack_into("<10I", header, 0x10, *fields)
            oversized.write_bytes(header)
            with self.assertRaisesRegex(retail_common.RetailError, "exceeds available payload"):
                identify_retail.parse_psx_exe(oversized)

    def test_identity_dry_run_writes_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "payload"
            payload.mkdir()
            (payload / "SYSTEM.CNF").write_text("BOOT = cdrom:\\BOOT.EXE;1\n", encoding="ascii")
            synthetic_exe(payload / "BOOT.EXE")
            manifest = root / "manifest.json"
            write_json(manifest, {"extraction": {"output_directory": str(payload)}})
            identity = root / "identity.json"
            before = manifest.read_bytes()
            result = identify_retail.main(
                [
                    str(payload),
                    "--manifest",
                    str(manifest),
                    "--identity",
                    str(identity),
                    "--dry-run",
                ]
            )
            self.assertEqual(result, 0)
            self.assertFalse(identity.exists())
            self.assertEqual(manifest.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
