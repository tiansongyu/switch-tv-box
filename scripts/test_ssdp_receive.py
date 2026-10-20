#!/usr/bin/env python3
"""Exercise SSDP reception with actual UDP packets and an unsupported ioctl."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix="switch-tv-box-ssdp-test-") as work:
        binary = str(Path(work) / "test_ssdp_receive")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
            "-ffunction-sections", "-fdata-sections",
            "-I", "scripts/iptv_test_stubs", "-I", "source",
            "scripts/test_ssdp_receive.c", "source/app/network_diagnostics.c",
            "-Wl,--gc-sections", "-pthread", "-o", binary,
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([binary], check=True, timeout=15)


if __name__ == "__main__":
    main()
