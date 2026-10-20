#!/usr/bin/env python3
"""Package the Windows runner's verified PyInstaller output and exact source."""
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "build/windows-discovery/Switch-TV-Box-Discovery"
DIST = ROOT / "dist"


def main():
    if platform.system() != "Windows":
        raise SystemExit("Build and verify the executable on the Windows runner first")
    executable = BUNDLE / "Switch-TV-Box-Discovery.exe"
    subprocess.run([str(executable), "--self-check"], check=True)
    shutil.copyfile(ROOT / "docs/EDEN-WINDOWS.txt", BUNDLE / "README-Windows.txt")
    source = BUNDLE / "source"
    source.mkdir(exist_ok=True)
    for filename in ("eden-lan-discovery.py", "eden_receiver.py", "requirements-eden-discovery.txt",
                     "test_eden_lan_discovery.py", "package_eden_windows.py"):
        shutil.copyfile(ROOT / "scripts" / filename, source / filename)
    licenses = BUNDLE / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "LICENSE", licenses / "LICENSE.Switch-TV-Box.txt")
    for package in ("psutil", "zeroconf", "ifaddr", "pyinstaller"):
        distribution = importlib.metadata.distribution(package)
        for file in distribution.files or []:
            if "license" in str(file).lower() or "copying" in str(file).lower():
                path = Path(distribution.locate_file(file))
                if path.is_file():
                    destination = licenses / package / Path(str(file)).name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, destination)
    python_license = Path(__import__("sys").base_prefix) / "LICENSE.txt"
    if python_license.exists():
        shutil.copyfile(python_license, licenses / "LICENSE.Python.txt")
    manifest = {
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "platform": platform.platform(), "python": platform.python_version(),
        "dependencies": {name: importlib.metadata.version(name) for name in
                         ("psutil", "zeroconf", "ifaddr", "pyinstaller")},
        "nro_compatibility": "Switch TV Box v0.1.1; media remains inside Eden",
        "files": {str(path.relative_to(BUNDLE)).replace("\\", "/"):
                  hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(BUNDLE.rglob("*")) if path.is_file()},
    }
    (BUNDLE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    DIST.mkdir(exist_ok=True)
    target = DIST / "switch-tv-box-eden-windows.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(BUNDLE.rglob("*")):
            if path.is_file():
                archive.write(path, "switch-tv-box-eden-windows/" + path.relative_to(BUNDLE).as_posix())
    checksum = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix(".zip.sha256").write_text(f"{checksum}  {target.name}\n", encoding="ascii")
    print(f"Packaged {target.name}: {target.stat().st_size} bytes; SHA256 {checksum}")


if __name__ == "__main__":
    main()
