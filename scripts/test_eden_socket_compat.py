#!/usr/bin/env python3
"""Check native socket behavior and isolated Eden ABI argument conversion."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix="switch-tv-box-socket-test-") as work:
        binary = str(Path(work) / "test_eden_socket_compat")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
            "-I", "source", "scripts/test_eden_socket_compat.c",
            "source/app/eden_socket_compat.c", "-o", binary,
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([binary], check=True, timeout=15)


if __name__ == "__main__":
    main()
