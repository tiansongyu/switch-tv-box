#!/usr/bin/env python3
"""Check real SSDP/description address selection with divergent host routes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

with tempfile.TemporaryDirectory(prefix="switch-tv-box-dlna-address-") as work:
    binary = str(Path(work) / "test_dlna_address")
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
        "-ffunction-sections", "-fdata-sections",
        "-I", "scripts/iptv_test_stubs", "-I", "source",
        "scripts/test_dlna_address.c", "source/app/network_diagnostics.c",
        "-Wl,--gc-sections", "-pthread", "-o", binary,
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=15)
