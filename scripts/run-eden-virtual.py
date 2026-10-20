#!/usr/bin/env python3
"""Run this project's Eden launcher in an isolated, authenticated Xvfb display."""

import os
from pathlib import Path
import secrets
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def stop(child, timeout=12):
    if child is None or child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def main():
    if not sys.argv[1:] or "--help" in sys.argv[1:]:
        print(__doc__)
        print("Usage: scripts/run-eden-virtual.py --interface enp7s0 [run-eden.py options]")
        print("Uses the existing Eden installation, a private X display, and the "
              "switch-tv-box profile. No VNC port is opened.")
        return 0
    for executable in ("Xvfb", "xauth", "xdpyinfo"):
        if not shutil.which(executable):
            raise RuntimeError(f"Required executable is missing: {executable}")

    logs = ROOT / "artifacts/validation"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / f"eden-xvfb-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}.log"
    stopped = False

    def request_stop(_signum, _frame):
        nonlocal stopped
        stopped = True

    previous = {sig: signal.signal(sig, request_stop)
                for sig in (signal.SIGTERM, signal.SIGINT)}
    xvfb = launcher = None
    try:
        with tempfile.TemporaryDirectory(prefix="switch-tv-box-x11-") as work:
            auth = Path(work) / "Xauthority"
            auth.touch(mode=0o600)
            cookie = secrets.token_hex(16)
            # X servers load the cookie independently of the address field.
            # The actual client display entry is added once -displayfd chooses
            # a free display; existing desktop and other tasks are untouched.
            subprocess.run(["xauth", "-f", str(auth), "add", ":0",
                            "MIT-MAGIC-COOKIE-1", cookie], check=True,
                           stdout=subprocess.DEVNULL)
            read_fd, write_fd = os.pipe()
            with log_path.open("wb") as log:
                try:
                    xvfb = subprocess.Popen(
                        ["Xvfb", "-displayfd", str(write_fd),
                         "-screen", "0", "1600x1000x24", "-nolisten", "tcp",
                         "-auth", str(auth)], pass_fds=(write_fd,),
                        stdout=log, stderr=subprocess.STDOUT,
                    )
                    os.close(write_fd)
                    write_fd = -1
                    if not select.select([read_fd], [], [], 10)[0]:
                        raise RuntimeError(f"Xvfb did not start; see {log_path}")
                    number = os.read(read_fd, 32).decode().strip()
                    if not number.isdigit() or xvfb.poll() is not None:
                        raise RuntimeError(f"Xvfb failed; see {log_path}")
                finally:
                    os.close(read_fd)
                    if write_fd >= 0:
                        os.close(write_fd)
                display = f":{number}"
                subprocess.run(["xauth", "-f", str(auth), "add", display,
                                "MIT-MAGIC-COOKIE-1", cookie], check=True,
                               stdout=subprocess.DEVNULL)
                environment = os.environ.copy()
                environment.update(DISPLAY=display, XAUTHORITY=str(auth))
                subprocess.run(["xdpyinfo"], env=environment, check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                print(f"Virtual desktop: DISPLAY={display}", flush=True)
                print(f"XAUTHORITY={auth}", flush=True)
                print(f"Xvfb log: {log_path}", flush=True)
                launcher = subprocess.Popen(
                    [sys.executable, str(ROOT / "scripts/run-eden.py"),
                     *sys.argv[1:]], env=environment, cwd=ROOT,
                )
                try:
                    while not stopped and launcher.poll() is None:
                        if xvfb.poll() is not None:
                            raise RuntimeError("The virtual display exited unexpectedly.")
                        time.sleep(0.2)
                    return 130 if stopped else launcher.returncode
                finally:
                    # Keep X11 alive while the launcher removes its own
                    # receiver, Avahi records and Eden process.
                    stop(launcher)
                    stop(xvfb, timeout=5)
    finally:
        stop(launcher)
        stop(xvfb, timeout=5)
        for sig, handler in previous.items():
            signal.signal(sig, handler)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"run-eden-virtual: {error}", file=sys.stderr)
        raise SystemExit(1)
