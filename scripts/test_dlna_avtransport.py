#!/usr/bin/env python3
"""Exercise actual DLNA parsing/actions/state with a controlled player owner."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

with tempfile.TemporaryDirectory(prefix="switch-tv-box-dlna-test-") as work:
    binary = str(Path(work) / "test_dlna_avtransport")
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-D_POSIX_C_SOURCE=200809L",
        "-DNXCAST_EXCLUSIVE_MEDIA_RESOURCES=1", "-Wall", "-Wextra", "-Werror",
        "-O1", "-ffunction-sections", "-fdata-sections",
        "-I", "scripts/iptv_test_stubs", "-I", "source",
        "scripts/test_dlna_avtransport.c",
        "source/protocol/dlna/control/handler.c",
        "source/protocol/dlna/control/soap_writer.c",
        "source/protocol/dlna/control/action/avtransport.c",
        "source/protocol/dlna/control/protocol_state.c",
        "source/protocol/dlna/control/controller_session.c",
        "source/player/seek_target.c", "source/player/trace.c",
        "-Wl,--gc-sections", "-lm", "-o", binary,
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=20)
