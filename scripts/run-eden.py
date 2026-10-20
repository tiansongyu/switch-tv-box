#!/usr/bin/env python3
"""Prepare an isolated Switch TV Box SD and run Eden on the current desktop."""

import argparse
import configparser
import contextlib
import fcntl
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import select
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
import zipfile


ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "dist/switch-tv-box-sdmc.zip"
APP = PurePosixPath("switch/switch-tv-box")
EDEN = Path.home() / ".local/bin/eden"
CONTROL_TEMPLATE = (
    Path.home() / ".local/share/netease-switch/eden/config/eden/qt-config.ini"
)
HELPER = ROOT / "scripts/eden-airplay-advertise.py"
HELPER_PYTHON = Path("/usr/bin/python3")
IPTV_DIRECT_HELPER = ROOT / "scripts/eden-iptv-direct.py"
IPTV_DIRECT_MARKER = "iptv-direct-http.flag"


def ini():
    result = configparser.ConfigParser(interpolation=None, delimiters=("=",))
    result.optionxform = str
    return result


def lan_address(interface, requested):
    if not interface or "/" in interface:
        raise ValueError("Specify a physical LAN interface, for example enp7s0.")
    if not (Path("/sys/class/net") / interface / "device").exists():
        raise ValueError(f"{interface}: not a physical network interface.")
    command = ["ip", "-j", "-4", "addr", "show", "dev", interface]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    addresses = [
        entry["local"]
        for link in json.loads(result.stdout)
        for entry in link.get("addr_info", [])
        if entry.get("family") == "inet" and entry.get("scope") == "global"
    ]
    if requested:
        address = str(ipaddress.IPv4Address(requested))
        if address not in addresses:
            raise ValueError(f"{address} is not assigned to {interface}.")
        return address
    if len(addresses) != 1:
        raise ValueError(
            f"{interface} has {len(addresses)} global IPv4 addresses; "
            "select one with --address."
        )
    return addresses[0]


def running_eden(state):
    """Also detect manually launched Eden children of a GDB session."""
    expected = {
        "XDG_CONFIG_HOME": str(state / "config"),
        "XDG_DATA_HOME": str(state / "data"),
        "EDEN_STATE_DIR": str(state),
    }
    matches = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            command = (proc / "cmdline").read_bytes().split(b"\0")
            names = [Path(os.fsdecode(value)).name.lower() for value in command if value]
            if not names:
                continue
            is_eden = names[0] in {"eden", "eden-cli"}
            is_debugger = names[0] == "gdb" and any(
                value in {"eden", "eden-cli"} for value in names[1:]
            )
            if not (is_eden or is_debugger):
                continue
            environ = {}
            for item in (proc / "environ").read_bytes().split(b"\0"):
                key, separator, value = item.partition(b"=")
                if separator and os.fsdecode(key) in expected:
                    environ[os.fsdecode(key)] = os.fsdecode(value)
            if any(
                Path(environ[key]).resolve() == Path(value)
                for key, value in expected.items()
                if environ.get(key)
            ):
                matches.append(int(proc.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
        except PermissionError as error:
            raise RuntimeError(
                f"Cannot inspect Eden PID {proc.name}; refusing to modify its possible SD."
            ) from error
    return matches


def require_stopped(state):
    pids = running_eden(state)
    if pids:
        raise RuntimeError(
            f"Eden/GDB already uses {state} (PID {', '.join(map(str, pids))}). "
            "Close that session before preparing or starting this profile."
        )


def checked_target(state, path):
    if not path.resolve().is_relative_to(state):
        raise ValueError(f"Path escapes the isolated state directory: {path}")
    return path


def atomic_write(state, target, data, mode=0o644):
    checked_target(state, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{target.name}.", dir=target.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        os.replace(temporary, target)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def package_files(archive):
    required = {
        str(APP / "switch-tv-box.nro"),
        str(APP / "fonts/switch_font.ttf"),
        str(APP / "iptv/sources.txt"),
        str(APP / "licenses/LICENSE.NX-Cast.txt"),
    }
    found = set()
    for entry in archive.infolist():
        path = PurePosixPath(entry.filename)
        if (
            path.is_absolute()
            or ".." in path.parts
            or "\\" in entry.filename
            or stat.S_ISLNK(entry.external_attr >> 16)
        ):
            raise ValueError(f"Unsafe SD package member: {entry.filename}")
        if entry.is_dir():
            continue
        if entry.filename == "README-switch-tv-box.txt":
            yield entry, path, True
            continue
        if not path.is_relative_to(APP):
            raise ValueError(f"Unexpected SD package member: {entry.filename}")
        relative = path.relative_to(APP)
        if not relative.parts:
            raise ValueError("The app directory cannot be a regular file.")
        top = relative.parts[0]
        # No identity, pairing, settings or runtime file is installed from ZIP.
        if top == "airplay" and relative != PurePosixPath("airplay/README.txt"):
            raise ValueError(f"Unexpected AirPlay runtime data in package: {relative}")
        if top not in {"switch-tv-box.nro", "fonts", "licenses", "dlna", "iptv", "airplay"}:
            raise ValueError(f"Unexpected app package member: {relative}")
        found.add(entry.filename)
        yield entry, path, top in {"switch-tv-box.nro", "fonts", "licenses", "dlna"}
    if required - found:
        raise ValueError(f"Incomplete SD package: missing {sorted(required - found)}")


def prepare(state, nro, interface):
    for directory in ("config/eden", "data/eden", "cache"):
        checked_target(state, state / directory).mkdir(parents=True, exist_ok=True)
    sd = state / "data/eden/sdmc"
    with zipfile.ZipFile(PACKAGE) as archive:
        # Validate the complete member list before making changes.
        members = list(package_files(archive))
        for entry, relative, replace in members:
            target = checked_target(state, sd.joinpath(*relative.parts))
            if replace or not target.exists():
                atomic_write(state, target, archive.read(entry))
    app = sd.joinpath(*APP.parts)
    atomic_write(state, app / "switch-tv-box.nro", nro.read_bytes())
    atomic_write(state, app / "eden-compat.flag", b"", mode=0o600)
    # This marker represents a live launcher-owned relay, never a persisted
    # preference. Preparing or launching without the option clears crash debris.
    checked_target(state, app / IPTV_DIRECT_MARKER).unlink(missing_ok=True)

    config_path = checked_target(state, state / "config/eden/qt-config.ini")
    config = ini()
    if config_path.exists():
        config.read(config_path)
    elif CONTROL_TEMPLATE.is_file():
        template = ini()
        template.read(CONTROL_TEMPLATE)
        if template.has_section("Controls"):
            # Never copy account tokens, paths, web services or user profiles.
            config["Controls"] = dict(template.items("Controls", raw=True))
    for section in ("Data%20Storage", "Services", "UI"):
        if not config.has_section(section):
            config.add_section(section)
    for name in ("nand", "sdmc", "load", "dump", "tas"):
        directory = checked_target(state, state / "data/eden" / name)
        directory.mkdir(parents=True, exist_ok=True)
        config["Data%20Storage"][f"{name}_directory"] = str(directory)
        config["Data%20Storage"][f"{name}_directory\\default"] = "false"
    config["Data%20Storage"]["save_directory"] = ""
    config["Data%20Storage"]["save_directory\\default"] = "true"
    config["Services"]["network_interface"] = interface
    config["Services"]["network_interface\\default"] = "false"
    screenshots = checked_target(state, state / "data/eden/screenshots")
    screenshots.mkdir(parents=True, exist_ok=True)
    config["UI"]["Screenshots\\screenshot_path"] = str(screenshots)
    config["UI"]["Screenshots\\screenshot_path\\default"] = "false"
    import io
    text = io.StringIO()
    config.write(text, space_around_delimiters=False)
    atomic_write(state, config_path, text.getvalue().encode(), mode=0o600)
    return app / "switch-tv-box.nro"


def preflight(args, state, nro):
    require_stopped(state)
    address = lan_address(args.interface, args.address)
    if not EDEN.is_file() or not os.access(EDEN, os.X_OK):
        raise ValueError(f"Existing Eden launcher not found or executable: {EDEN}")
    with nro.open("rb") as stream:
        header = stream.read(0x80)
    if len(header) < 0x80 or header[0x10:0x14] != b"NRO0":
        raise ValueError(f"Not a Switch NRO: {nro}")
    with zipfile.ZipFile(PACKAGE) as archive:
        list(package_files(archive))
        damaged = archive.testzip()
        if damaged:
            raise ValueError(f"Damaged SD package member: {damaged}")
    if args.iptv_direct_http:
        if not IPTV_DIRECT_HELPER.is_file():
            raise ValueError(f"IPTV direct HTTP helper is missing: {IPTV_DIRECT_HELPER}")
        subprocess.run(
            [str(HELPER_PYTHON), str(IPTV_DIRECT_HELPER),
             "--interface", args.interface, "--check"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
    if not args.prepare_only:
        if not os.environ.get("DISPLAY"):
            raise ValueError("DISPLAY is unset; run from the current desktop session.")
        subprocess.run(
            ["xdpyinfo", "-display", os.environ["DISPLAY"]],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        if not HELPER.is_file():
            raise ValueError(f"AirPlay discovery helper is missing: {HELPER}")
        subprocess.run(
            [
                str(HELPER_PYTHON), "-c",
                "import dbus; from gi.repository import GLib; "
                "dbus.SystemBus().get_name_owner('org.freedesktop.Avahi')",
            ],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            # Match the receiver socket: old connections in TIME_WAIT must
            # not prevent restarting an otherwise stopped emulator.
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("0.0.0.0", 7000))
    return address


def stop_child(child):
    if child is None or child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(timeout=8)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


@contextlib.contextmanager
def iptv_direct_session(state, interface, log, diagnostics=False):
    """Publish the runtime marker only after this process's relay is ready."""
    child = None
    identity = None
    marker = checked_target(
        state, state / "data/eden/sdmc" / Path(*APP.parts) / IPTV_DIRECT_MARKER
    )
    read_fd, write_fd = os.pipe()
    try:
        command = [
            str(HELPER_PYTHON), str(IPTV_DIRECT_HELPER),
            "--interface", interface, "--parent-pid", str(os.getpid()),
            "--ready-fd", str(write_fd),
        ]
        if diagnostics:
            command.append("--diagnostics")
        child = subprocess.Popen(
            command,
            cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
            pass_fds=(write_fd,), start_new_session=True,
        )
        os.close(write_fd)
        write_fd = -1
        if (not select.select([read_fd], [], [], 5)[0] or
                os.read(read_fd, 1) != b"R" or child.poll() is not None):
            raise RuntimeError("IPTV direct HTTP helper did not become ready; see its log.")
        atomic_write(state, marker, f"launcher_pid={os.getpid()}\n".encode(), mode=0o600)
        current = marker.stat()
        identity = (current.st_dev, current.st_ino)
        yield child
    finally:
        os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)
        stop_child(child)
        if identity is not None:
            with contextlib.suppress(FileNotFoundError):
                current = marker.stat()
                if (current.st_dev, current.st_ino) == identity:
                    marker.unlink()


def launch(state, nro, interface, address, lock_fd, iptv_direct_http=False,
           iptv_direct_http_diagnostics=False):
    logs = ROOT / "artifacts/validation"
    logs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S") + f"-{os.getpid()}"
    eden_log_path = logs / f"eden-desktop-{stamp}.log"
    helper_log_path = logs / f"eden-discovery-{stamp}.log"
    direct_log_path = logs / f"eden-iptv-direct-{stamp}.log"
    environment = os.environ.copy()
    environment.update({
        "XDG_CONFIG_HOME": str(state / "config"),
        "XDG_DATA_HOME": str(state / "data"),
        "XDG_CACHE_HOME": str(state / "cache"),
        "EDEN_STATE_DIR": str(state),
        "QT_QPA_PLATFORM": "xcb",
    })
    eden = helper = None
    direct = None
    direct_resources = contextlib.ExitStack()
    stop_requested = False

    def request_stop(_signum, _frame):
        nonlocal stop_requested
        stop_requested = True

    old_handlers = {
        number: signal.signal(number, request_stop)
        for number in (signal.SIGINT, signal.SIGTERM)
    }
    try:
        if iptv_direct_http:
            direct_log = direct_resources.enter_context(direct_log_path.open("wb"))
            direct = direct_resources.enter_context(
                iptv_direct_session(state, interface, direct_log, iptv_direct_http_diagnostics))
            print(f"IPTV direct HTTP enabled; public IPv4 HTTP only; log: {direct_log_path}",
                  flush=True)
        with eden_log_path.open("wb") as eden_log, helper_log_path.open("wb") as helper_log:
            eden = subprocess.Popen(
                [str(EDEN), "-g", str(nro)], env=environment, cwd=ROOT,
                stdout=eden_log, stderr=subprocess.STDOUT, start_new_session=True,
                pass_fds=(lock_fd,),
            )
            helper_command = [
                str(HELPER_PYTHON), str(HELPER), "--pid", str(eden.pid),
                "--interface", interface, "--address", address, "--port", "7000",
            ]
            print(f"Eden PID {eden.pid}; desktop {environment['DISPLAY']}", flush=True)
            print(f"Eden log: {eden_log_path}", flush=True)
            print(f"AirPlay discovery log: {helper_log_path}", flush=True)
            print(
                "Waiting for this NRO's AirPlay listener; choose No if Eden "
                "asks for decryption keys.",
                flush=True,
            )
            # The helper requires both PID-owned port 7000 and a complete /info.
            # A first-run Qt modal may hold boot, so wait while the GUI lives.
            last_error = None
            while eden.poll() is None and not stop_requested:
                if direct is not None and direct.poll() is not None:
                    raise RuntimeError("IPTV direct HTTP helper exited; stopping this Eden session.")
                try:
                    probe = subprocess.run(
                        helper_command + ["--check"], cwd=ROOT,
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=6,
                    )
                    if probe.returncode == 0:
                        break
                    if probe.stderr != last_error:
                        helper_log.write(probe.stderr)
                        helper_log.flush()
                        last_error = probe.stderr
                except subprocess.TimeoutExpired:
                    pass
                time.sleep(0.5)
            if stop_requested or eden.poll() is not None:
                return 130 if stop_requested else eden.returncode
            helper = subprocess.Popen(
                helper_command,
                cwd=ROOT, stdout=helper_log, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            warned = False
            while eden.poll() is None and not stop_requested:
                if direct is not None and direct.poll() is not None:
                    raise RuntimeError("IPTV direct HTTP helper exited; stopping this Eden session.")
                if helper.poll() is not None and not warned:
                    print(
                        f"AirPlay helper exited ({helper.returncode}); "
                        f"see {helper_log_path}. Eden remains available.",
                        file=sys.stderr, flush=True,
                    )
                    warned = True
                time.sleep(0.25)
            return 130 if stop_requested else eden.returncode
    finally:
        # Signal only the subprocesses created by this launcher.
        stop_child(helper)
        stop_child(eden)
        direct_resources.close()
        for number, handler in old_handlers.items():
            signal.signal(number, handler)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=(
            "Requires the Eden-compatible deko3d build of the NRO, a current X11 "
            "desktop, iproute2, xdpyinfo, Avahi, python3-dbus and python3-gi. "
            "No virtual desktop is created."
        ),
    )
    parser.add_argument("--interface", required=True, help="physical LAN interface (e.g. enp7s0)")
    parser.add_argument("--address", help="IPv4 on that interface; inferred if exactly one is assigned")
    parser.add_argument("--state", type=Path,
                        default=Path.home() / ".local/share/switch-tv-box/eden",
                        help="isolated Eden state directory")
    parser.add_argument("--nro", type=Path, default=ROOT / "switch-tv-box.nro",
                        help="NRO to install over the packaged executable")
    parser.add_argument("--iptv-direct-http", action="store_true",
                        help="optional Eden relay through --interface; public IPv4 HTTP IPTV only "
                             "(no domains or HTTPS, no system proxy changes)")
    parser.add_argument("--iptv-direct-http-diagnostics", action="store_true",
                        help="with --iptv-direct-http, log bounded timings/framing without URLs")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true",
                      help="install SD resources and configure Eden without starting it")
    mode.add_argument("--check", action="store_true",
                      help="read-only preflight; do not prepare or launch")
    args = parser.parse_args()
    if args.iptv_direct_http_diagnostics and not args.iptv_direct_http:
        parser.error("--iptv-direct-http-diagnostics requires --iptv-direct-http")
    state, nro = args.state.expanduser().resolve(), args.nro.expanduser().resolve()
    try:
        address = preflight(args, state, nro)
        if args.check:
            print(f"Preflight passed: {args.interface} {address}; state {state}")
            return 0
        state.mkdir(parents=True, exist_ok=True)
        lock_path = checked_target(state, state / "run.lock")
        with lock_path.open("a+b") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise RuntimeError(f"Another launcher holds {lock_path}.") from error
            # Recheck after acquiring the lock, before changing configuration or SD.
            require_stopped(state)
            installed = prepare(state, nro, args.interface)
            print(f"Prepared {installed}; LAN {args.interface} {address}", flush=True)
            if args.prepare_only:
                return 0
            return launch(state, installed, args.interface, address, lock.fileno(),
                          args.iptv_direct_http, args.iptv_direct_http_diagnostics)
    except (OSError, ValueError, RuntimeError, configparser.Error,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f"run-eden: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            detail = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr
            print(detail.strip(), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
