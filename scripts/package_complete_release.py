#!/usr/bin/env python3
"""Package the existing SD release and committed source; never build or publish."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile


NRO_NAME = "switch/switch-tv-box/switch-tv-box.nro"
SD_ZIP_NAME = "switch-tv-box-sdmc.zip"
FORBIDDEN_PARTS = {".git", ".runtime", "artifacts", "keys"}
SECRET_NAMES = {"identity.bin", "pairings.bin", "prod.keys", "title.keys", "console.keys"}
SECRET_SUFFIXES = {".key", ".pem", ".p12", ".pfx", ".keys"}
# Required by the existing SD installer/runtime; examples may remain optional.
REQUIRED_RUNTIME_FILES = (
    "README-switch-tv-box.txt",
    NRO_NAME,
    "switch/switch-tv-box/fonts/switch_font.ttf",
    "switch/switch-tv-box/fonts/FREEWARE.ControllerFont.txt",
    "switch/switch-tv-box/fonts/LICENSE.SourceFont.txt",
    "switch/switch-tv-box/dlna/Description.xml",
    "switch/switch-tv-box/dlna/AVTransport.xml",
    "switch/switch-tv-box/dlna/ConnectionManager.xml",
    "switch/switch-tv-box/dlna/RenderingControl.xml",
    "switch/switch-tv-box/dlna/SinkProtocolInfo.csv",
    "switch/switch-tv-box/dlna/Presentation.html",
    "switch/switch-tv-box/dlna/icon.jpg",
    "switch/switch-tv-box/iptv/sources.txt",
    "switch/switch-tv-box/iptv/README.txt",
    "switch/switch-tv-box/airplay/README.txt",
    "switch/switch-tv-box/licenses/LICENSE.NX-Cast.txt",
    "switch/switch-tv-box/licenses/LICENSE.PlayFair.GPLv3.txt",
    "switch/switch-tv-box/licenses/LICENSE.Dear-ImGui.txt",
    "switch/switch-tv-box/licenses/LICENSE.libsodium.txt",
    "switch/switch-tv-box/licenses/LICENSE.RemixIcon.MIT.txt",
    "switch/switch-tv-box/licenses/THIRD-PARTY-NOTICE.txt",
)


class PackageError(RuntimeError):
    pass


def git(root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return sha256_stream(stream)


def sha256_stream(stream) -> str:
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def safe_name(name: str, directory: bool = False) -> str:
    stripped = name[:-1] if directory and name.endswith("/") else name
    parts = stripped.split("/")
    if not stripped or "\\" in name or "\0" in name or any(
        part in ("", ".", "..") or ":" in part for part in parts
    ):
        raise PackageError(f"Unsafe archive path: {name!r}")
    if any(part.lower() in FORBIDDEN_PARTS for part in parts):
        raise PackageError(f"Local/private directory cannot be packaged: {name!r}")
    filename = parts[-1].lower()
    if filename in SECRET_NAMES or Path(filename).suffix in SECRET_SUFFIXES:
        raise PackageError(f"Key or receiver identity cannot be packaged: {name!r}")
    return stripped


def inspect_zip(archive: zipfile.ZipFile) -> set[str]:
    names: set[str] = set()
    files: set[str] = set()
    for entry in archive.infolist():
        if entry.orig_filename != entry.filename:
            raise PackageError(f"Ambiguous archive path: {entry.orig_filename!r}")
        name = safe_name(entry.filename, entry.is_dir())
        if name in names:
            raise PackageError(f"Duplicate archive entry: {name}")
        names.add(name)
        mode = entry.external_attr >> 16
        kind = stat.S_IFMT(mode)
        if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise PackageError(f"Links and special files cannot be packaged: {name}")
        if entry.flag_bits & 1:
            raise PackageError(f"Encrypted archive entry: {name}")
        if not entry.is_dir():
            files.add(name)
    for name in names:
        parts = name.split("/")
        if any("/".join(parts[:index]) in files for index in range(1, len(parts))):
            raise PackageError(f"Archive file shadows a parent directory: {name}")
    bad = archive.testzip()
    if bad is not None:
        raise PackageError(f"ZIP CRC verification failed: {bad}")
    return files


def copy_archive(source: zipfile.ZipFile, target: zipfile.ZipFile, prefix: str) -> None:
    for entry in source.infolist():
        copied = zipfile.ZipInfo(prefix + entry.filename, entry.date_time)
        copied.create_system = 3
        copied.compress_type = zipfile.ZIP_DEFLATED
        mode = entry.external_attr >> 16
        permissions = mode & 0o777 or (0o755 if entry.is_dir() else 0o644)
        copied.external_attr = ((stat.S_IFDIR if entry.is_dir() else stat.S_IFREG) | permissions) << 16
        if entry.is_dir():
            target.writestr(copied, b"")
        else:
            with source.open(entry) as incoming, target.open(copied, "w") as outgoing:
                shutil.copyfileobj(incoming, outgoing, 1024 * 1024)


def package_release(root: Path, expected_nro_sha256: str | None = None) -> Path:
    if expected_nro_sha256 is not None:
        if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_nro_sha256):
            raise PackageError("Expected NRO SHA-256 must contain exactly 64 hexadecimal characters.")
        expected_nro_sha256 = expected_nro_sha256.lower()
    root = root.resolve()
    if Path(git(root, "rev-parse", "--show-toplevel").decode().strip()).resolve() != root:
        raise PackageError("Run against the repository root.")
    if git(root, "status", "--porcelain", "--untracked-files=no"):
        raise PackageError("Tracked source changes must be committed before packaging.")
    commit = git(root, "rev-parse", "HEAD").decode().strip()
    makefile = git(root, "show", f"{commit}:makefile").decode()
    version_match = re.search(
        r"^APP_VERSION\s*:=\s*(\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?)\s*$", makefile, re.M
    )
    if not version_match:
        raise PackageError("APP_VERSION in the committed makefile is invalid.")
    version = version_match.group(1)
    tracked: set[str] = set()
    for record in git(root, "ls-tree", "-r", "-z", commit).split(b"\0"):
        if not record:
            continue
        metadata, encoded_name = record.split(b"\t", 1)
        mode, kind, _object = metadata.split()
        name = safe_name(encoded_name.decode("utf-8"))
        if kind != b"blob" or mode not in (b"100644", b"100755"):
            raise PackageError(f"Source archive requires a regular tracked file: {name}")
        tracked.add(name)

    dist = root / "dist"
    sd_path = dist / SD_ZIP_NAME
    nro_path = root / "switch-tv-box.nro"
    if not sd_path.is_file() or not nro_path.is_file():
        raise PackageError("Build the NRO and dist/switch-tv-box-sdmc.zip before packaging.")
    nro_hash = sha256_file(nro_path)
    if expected_nro_sha256 is not None and nro_hash != expected_nro_sha256:
        raise PackageError("NRO does not match the caller-provided acceptance SHA-256.")
    sd_hash = sha256_file(sd_path)
    output = dist / f"switch-tv-box-v{version}-complete.zip"

    with tempfile.TemporaryDirectory(prefix=".complete-release-", dir=dist) as temporary:
        staging = Path(temporary)
        source_path = staging / "source.zip"
        with source_path.open("wb") as stream:
            subprocess.run(["git", "-C", str(root), "archive", "--format=zip", commit],
                           stdout=stream, stderr=subprocess.PIPE, check=True)
        complete_path = staging / "complete.zip"
        with zipfile.ZipFile(sd_path) as sd, zipfile.ZipFile(source_path) as source:
            sd_files = inspect_zip(sd)
            source_files = inspect_zip(source)
            if source_files != tracked:
                raise PackageError("git archive omitted tracked files; check export-ignore attributes.")
            if {name for name in sd_files if name.lower().endswith(".nro")} != {NRO_NAME}:
                raise PackageError("SD ZIP must contain exactly the expected switch-tv-box NRO.")
            for name in REQUIRED_RUNTIME_FILES:
                if name not in sd_files or sd.getinfo(name).file_size == 0:
                    raise PackageError(f"Required runtime file is missing or empty: {name}")
            with sd.open(NRO_NAME) as stream:
                if sha256_stream(stream) != nro_hash:
                    raise PackageError("SD ZIP NRO does not match the repository-root build artifact.")
            manifest = {
                "format": "switch-tv-box-complete-release-v1",
                "app_version": version,
                "source_commit": commit,
                "source_files": len(source_files),
                "sd_files": len(sd_files),
                "nro": {"path": "sdmc/" + NRO_NAME, "sha256": nro_hash},
                "input_sd_zip": {"filename": SD_ZIP_NAME, "sha256": sd_hash},
                # This records a caller's accepted artifact, not a playback test
                # performed by this packaging script or by the draft CI build.
                "acceptance": (
                    {"status": "hash_matched", "expected_nro_sha256": expected_nro_sha256}
                    if expected_nro_sha256 is not None else {"status": "not_provided"}
                ),
            }
            acceptance_note = (
                "NRO 与调用方提供的验收 SHA-256 一致；验收 hash 记录在 manifest.json。\n"
                "The NRO matches the caller-provided acceptance hash in manifest.json.\n"
                if expected_nro_sha256 is not None else
                "本包未提供验收 hash；打包成功不代表播放验收通过。\n"
                "No acceptance hash was provided; packaging does not certify playback.\n"
            )
            readme = f"""Switch TV Box v{version} — 完整发布包 / Complete release

安装：将 sdmc/ 文件夹里面的内容复制到 Switch SD 卡根目录。
最终程序路径：switch/switch-tv-box/switch-tv-box.nro
使用 hbmenu 启动程序；请一并复制字体、直播源和 DLNA 等配套文件。

Install: copy the CONTENTS of sdmc/ to the root of your Switch SD card.
Launch switch-tv-box from hbmenu. Keep all accompanying runtime assets.

source/：提交 {commit} 的完整已提交源码。
离线 AirPlay 文档：source/docs/reference/
第三方源码和许可：source/third_party/
构建说明：source/README.md

source/ contains committed source, offline AirPlay reference material and
third-party code/licenses. Build instructions are in source/README.md.
manifest.json records the source commit and NRO/input SD ZIP SHA-256 hashes.
The input SD ZIP is expanded under sdmc/ rather than nested as another ZIP.

{acceptance_note}"""
            with zipfile.ZipFile(complete_path, "w", compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=6) as complete:
                copy_archive(sd, complete, "sdmc/")
                copy_archive(source, complete, "source/")
                complete.writestr("README.txt", readme)
                complete.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        with zipfile.ZipFile(complete_path) as complete:
            inspect_zip(complete)
        if git(root, "status", "--porcelain", "--untracked-files=no") or \
           git(root, "rev-parse", "HEAD").decode().strip() != commit:
            raise PackageError("Source changed while packaging; retry after committing.")
        if sha256_file(nro_path) != nro_hash or sha256_file(sd_path) != sd_hash:
            raise PackageError("Build artifacts changed while packaging; retry after building.")
        complete_hash = sha256_file(complete_path)
        checksums = staging / "SHA256SUMS.txt"
        checksums.write_text(f"{sd_hash}  {SD_ZIP_NAME}\n{complete_hash}  {output.name}\n")
        complete_path.replace(output)
        checksums.replace(dist / "SHA256SUMS.txt")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-nro-sha256", metavar="SHA256",
                        help="Bind this package to a caller-accepted NRO hash; no playback test is run.")
    args = parser.parse_args()
    try:
        output = package_release(Path(__file__).resolve().parent.parent, args.expected_nro_sha256)
    except (PackageError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"[complete-release] {error}", file=sys.stderr)
        return 1
    print(f"[complete-release] Verified and created {output}")
    print(f"[complete-release] Checksums: {output.parent / 'SHA256SUMS.txt'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
