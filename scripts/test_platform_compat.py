#!/usr/bin/env python3
"""Exercise the actual select wrapper with host UDP, threads and signals."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix="switch-tv-box-platform-test-") as work:
        binary = str(Path(work) / "test_platform_compat")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
            "-I", "source", "scripts/test_platform_compat.c",
            "source/app/platform_compat.c", "-pthread",
            "-Wl,--wrap=select", "-Wl,--wrap=access", "-o", binary,
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        for mode in ("native", "eden"):
            subprocess.run([binary, mode], check=True, timeout=15)


if __name__ == "__main__":
    main()
