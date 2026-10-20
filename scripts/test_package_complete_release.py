#!/usr/bin/env python3
"""Local Git/ZIP fixtures; no builds, keys, emulator or network."""
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile


SCRIPT = Path(__file__).with_name("package_complete_release.py")
SPEC = importlib.util.spec_from_file_location("complete_release", SCRIPT)
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class CompleteReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="tvbox-complete-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.git("init", "--initial-branch=main")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        files = {
            "makefile": "APP_VERSION := 0.1.0\n",
            ".gitignore": "dist/\nswitch-tv-box.nro\n.runtime/\nartifacts/\nkeys/\n",
            "source/main.c": "int main(void) { return 0; }\n",
            "docs/reference/airplay-spec/index.html": "<h1>Offline reference</h1>\n",
            "third_party/example/LICENSE": "License fixture\n",
        }
        for name, text in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.git("add", ".")
        self.git("commit", "-m", "fixture")
        self.nro = b"NRO fixture: this is not executable"
        (self.root / "switch-tv-box.nro").write_bytes(self.nro)
        (self.root / "dist").mkdir()
        self.sd = self.root / "dist" / package.SD_ZIP_NAME
        self.write_sd()

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.PIPE)

    def write_sd(self, extra=None, nro=None, omit=None, empty=None):
        files = {name: f"Runtime fixture: {name}\n".encode()
                 for name in package.REQUIRED_RUNTIME_FILES}
        files[package.NRO_NAME] = self.nro if nro is None else nro
        with zipfile.ZipFile(self.sd, "w", compression=zipfile.ZIP_STORED) as archive:
            for name, data in files.items():
                if name != omit:
                    archive.writestr(name, b"" if name == empty else data)
            if extra is not None:
                archive.writestr(extra, b"do not include")

    def test_complete_contents_hashes_and_untracked_exclusion(self):
        for name in (".runtime/session", "artifacts/private.log", "keys/prod.keys", "personal.txt"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("must not be packaged")
        output = package.package_release(self.root)
        self.assertEqual(output.name, "switch-tv-box-v0.1.0-complete.zip")
        with zipfile.ZipFile(output) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertIn("source/docs/reference/airplay-spec/index.html", names)
            self.assertIn("source/third_party/example/LICENSE", names)
            self.assertNotIn("source/personal.txt", names)
            self.assertFalse(any(part in package.FORBIDDEN_PARTS for name in names for part in name.split("/")))
            self.assertEqual(archive.read("sdmc/" + package.NRO_NAME), self.nro)
            manifest = json.loads(archive.read("manifest.json"))
            self.assertEqual(manifest["source_commit"], self.git("rev-parse", "HEAD").decode().strip())
            self.assertEqual(manifest["nro"]["sha256"], hashlib.sha256(self.nro).hexdigest())
            self.assertEqual(manifest["input_sd_zip"]["sha256"], package.sha256_file(self.sd))
            self.assertEqual(manifest["source_files"], 5)
            self.assertEqual(manifest["acceptance"], {"status": "not_provided"})
            self.assertIn(b"No acceptance hash was provided", archive.read("README.txt"))
        for line in (output.parent / "SHA256SUMS.txt").read_text().splitlines():
            expected, name = line.split("  ", 1)
            self.assertEqual(expected, package.sha256_file(output.parent / name))

    def test_dirty_tracked_source_and_staged_changes_rejected(self):
        (self.root / "source/main.c").write_text("changed\n")
        with self.assertRaisesRegex(package.PackageError, "committed"):
            package.package_release(self.root)
        self.git("add", "source/main.c")
        with self.assertRaisesRegex(package.PackageError, "committed"):
            package.package_release(self.root)

    def test_missing_or_empty_runtime_assets_rejected(self):
        for name in package.REQUIRED_RUNTIME_FILES:
            for condition in ("omit", "empty"):
                with self.subTest(name=name, condition=condition):
                    self.write_sd(**{condition: name})
                    with self.assertRaises(package.PackageError):
                        package.package_release(self.root)

    def test_accepted_hash_cli_binding_and_manifest(self):
        script = self.root / "scripts" / SCRIPT.name
        script.parent.mkdir()
        script.write_bytes(SCRIPT.read_bytes())
        accepted_hash = hashlib.sha256(self.nro).hexdigest()
        result = subprocess.run([sys.executable, str(script), "--expected-nro-sha256",
                                 accepted_hash.upper()], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.root / "dist/switch-tv-box-v0.1.0-complete.zip") as archive:
            manifest = json.loads(archive.read("manifest.json"))
            self.assertEqual(manifest["acceptance"], {
                "status": "hash_matched", "expected_nro_sha256": accepted_hash})
            self.assertIn(b"matches the caller-provided acceptance hash", archive.read("README.txt"))

    def test_wrong_or_malformed_acceptance_hash_rejected(self):
        for value in ("0" * 64, "", "a" * 63, "g" * 64, "a" * 65):
            with self.subTest(value=value):
                with self.assertRaises(package.PackageError):
                    package.package_release(self.root, value)
        self.assertFalse((self.root / "dist/switch-tv-box-v0.1.0-complete.zip").exists())

    def test_unsafe_sd_paths_and_private_files_rejected(self):
        for name in ("../escape", "/absolute", "switch/../../escape", r"C:\escape",
                     "C:/escape", "switch//duplicate", ".git/config", ".runtime/state",
                     "artifacts/private.log", "keys/prod.keys", "switch/identity.bin"):
            with self.subTest(name=name):
                self.write_sd(name)
                with self.assertRaises(package.PackageError):
                    package.package_release(self.root)

    def test_duplicate_link_and_nro_mismatch_rejected(self):
        self.write_sd(nro=b"stale NRO")
        with self.assertRaisesRegex(package.PackageError, "does not match"):
            package.package_release(self.root)
        self.write_sd()
        with zipfile.ZipFile(self.sd, "a") as archive:
            link = zipfile.ZipInfo("switch/link")
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(link, "/outside")
        with self.assertRaisesRegex(package.PackageError, "Links"):
            package.package_release(self.root)
        self.write_sd("switch/another.nro")
        with self.assertRaisesRegex(package.PackageError, "exactly"):
            package.package_release(self.root)
        self.write_sd()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(self.sd, "a") as archive:
                archive.writestr(package.NRO_NAME, self.nro)
        with self.assertRaisesRegex(package.PackageError, "Duplicate"):
            package.package_release(self.root)

    def test_crc_failure_rejected(self):
        damaged = self.sd.read_bytes().replace(self.nro, b"X" + self.nro[1:], 1)
        self.sd.write_bytes(damaged)
        with self.assertRaises((package.PackageError, zipfile.BadZipFile)):
            package.package_release(self.root)

    def test_tracked_private_directory_rejected(self):
        (self.root / ".runtime").mkdir()
        (self.root / ".runtime/private").write_text("secret")
        self.git("add", "-f", ".runtime/private")
        self.git("commit", "-m", "unsafe source fixture")
        with self.assertRaisesRegex(package.PackageError, "private"):
            package.package_release(self.root)

    def test_export_ignore_cannot_silently_omit_committed_source(self):
        (self.root / ".gitattributes").write_text("docs/ export-ignore\n")
        self.git("add", ".gitattributes")
        self.git("commit", "-m", "export exclusion fixture")
        with self.assertRaisesRegex(package.PackageError, "omitted tracked files"):
            package.package_release(self.root)

    def test_failed_package_preserves_previous_release(self):
        output = package.package_release(self.root)
        before = output.read_bytes()
        checksums_before = (output.parent / "SHA256SUMS.txt").read_bytes()
        self.write_sd(nro=b"stale")
        with self.assertRaises(package.PackageError):
            package.package_release(self.root)
        self.assertEqual(output.read_bytes(), before)
        self.assertEqual((output.parent / "SHA256SUMS.txt").read_bytes(), checksums_before)


if __name__ == "__main__":
    unittest.main()
