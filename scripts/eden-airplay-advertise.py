#!/usr/bin/env python3
"""Publish an Eden receiver's live AirPlay identity on one physical LAN.

Eden 0.2.1 ignores IP multicast socket options. This helper publishes discovery
through Avahi; audio, video, pairing and control remain in the running NRO.
The ownership/lifetime checks follow netease-switch's discovery helper.
"""

import argparse
import fcntl
import ipaddress
import json
import os
from pathlib import Path
import plistlib
import re
import signal
import socket
import struct
import time


FEATURE_AUDIO = 1 << 9
FEATURE_RSA = 1 << 23
MAX_REPLY = 65536


def process_identity(pid):
    # comm may contain spaces or parentheses; fields after its last ')' start
    # with field 3, and starttime is field 22.
    return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]


def listener_identity(pid, address, port):
    """Return the inode only when this process owns the IPv4 TCP listener."""
    inodes = set()
    for fd in Path(f"/proc/{pid}/fd").iterdir():
        try:
            match = re.fullmatch(r"socket:\[(\d+)\]", os.readlink(fd))
            if match:
                inodes.add(match[1])
        except FileNotFoundError:
            pass
    address_hex = socket.inet_aton(address)[::-1].hex().upper()
    endpoints = {f"{address_hex}:{port:04X}", f"00000000:{port:04X}"}
    for row in Path(f"/proc/{pid}/net/tcp").read_text().splitlines()[1:]:
        fields = row.split()
        if fields[1] in endpoints and fields[3] == "0A" and fields[9] in inodes:
            return fields[9]
    return None


def listening(pid, address, port):
    return listener_identity(pid, address, port) is not None


def interface_address(interface):
    encoded = interface.encode("ascii")
    if len(encoded) >= 16:
        raise ValueError("Interface name is too long")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        result = fcntl.ioctl(sock, 0x8915, struct.pack("256s", encoded))
    return socket.inet_ntoa(result[20:24])


def get_info(address, port, qualifier=False):
    """Use a direct socket, so HTTP_PROXY/ALL_PROXY cannot redirect discovery."""
    body = plistlib.dumps({"qualifier": ["txtAirPlay"]},
                         fmt=plistlib.FMT_BINARY) if qualifier else b""
    request = (
        "GET /info RTSP/1.0\r\nCSeq: 1\r\n"
        "Content-Type: application/x-apple-binary-plist\r\n"
        f"Content-Length: {len(body)}\r\n\r\n"
    ).encode("ascii") + body
    with socket.create_connection((address, port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(request)
        reply = b""
        while b"\r\n\r\n" not in reply:
            chunk = sock.recv(4096)
            if not chunk:
                raise ValueError("Receiver closed before /info headers")
            reply += chunk
            if len(reply) > MAX_REPLY:
                raise ValueError("/info headers exceed limit")
        headers, payload = reply.split(b"\r\n\r\n", 1)
        lines = headers.split(b"\r\n")
        if lines[0].split()[:2] != [b"RTSP/1.0", b"200"]:
            raise ValueError(f"/info failed: {lines[0]!r}")
        lengths = [line.split(b":", 1)[1].strip() for line in lines[1:]
                   if line.lower().startswith(b"content-length:")]
        if len(lengths) != 1 or not lengths[0].isdigit():
            raise ValueError("/info has invalid Content-Length")
        size = int(lengths[0])
        if not 0 < size <= MAX_REPLY:
            raise ValueError("/info body exceeds limit")
        while len(payload) < size:
            chunk = sock.recv(min(4096, size - len(payload)))
            if not chunk:
                raise ValueError("Receiver closed during /info body")
            payload += chunk
    result = plistlib.loads(payload[:size])
    if not isinstance(result, dict):
        raise ValueError("/info is not a dictionary")
    return result


def decode_txt(raw):
    if not isinstance(raw, bytes) or not raw:
        raise ValueError("Receiver did not provide txtAirPlay")
    values = {}
    pos = 0
    while pos < len(raw):
        size = raw[pos]
        pos += 1
        if not size or size > len(raw) - pos:
            raise ValueError("Malformed txtAirPlay item")
        item = raw[pos:pos + size]
        pos += size
        if b"=" not in item:
            raise ValueError("Malformed txtAirPlay key/value")
        key, value = item.decode("utf-8").split("=", 1)
        if key in values:
            raise ValueError("Duplicate txtAirPlay key")
        values[key] = value
    return values


def feature_bits(value):
    words = value.split(",")
    if len(words) not in (1, 2):
        raise ValueError("Malformed feature bits")
    bits = [int(word, 0) for word in words]
    if any(not 0 <= word <= 0xFFFFFFFF for word in bits):
        raise ValueError("Feature word exceeds 32 bits")
    return bits[0] | ((bits[1] << 32) if len(bits) == 2 else 0)


def service_records(info, airplay):
    """Match discovery/mdns.c using the running receiver's capability mask."""
    device = str(info["deviceID"]).upper()
    if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", device):
        raise ValueError("Invalid receiver deviceID")
    name = info["name"]
    compact_device = device.replace(":", "")
    if not isinstance(name, str) or not name or "\0" in name:
        raise ValueError("Invalid receiver name")
    if len(f"{compact_device}@{name}".encode("utf-8")) > 63:
        raise ValueError("Receiver RAOP service name exceeds one DNS label")
    features = info["features"]
    if not isinstance(features, int) or not 0 < features < (1 << 64):
        raise ValueError("Invalid receiver features")
    public_key = info["pk"]
    if not isinstance(public_key, bytes) or len(public_key) != 32:
        raise ValueError("Invalid receiver public key")
    if (airplay["deviceid"].upper() != device
            or feature_bits(airplay["features"]) != features
            or airplay["pk"].lower() != public_key.hex()
            or airplay["pi"] != info["pi"]):
        raise ValueError("/info and txtAirPlay identities do not match")
    if airplay["pw"] not in ("true", "false"):
        raise ValueError("Unsupported receiver PIN declaration")
    legacy = bool(features & FEATURE_RSA)
    # receiver.c ties RSA bit 23 directly to legacy_audio_open_callback.
    require_pin = airplay["pw"] == "true" and not legacy
    metadata = ",".join(str(index) for index, bit in enumerate((17, 15, 16))
                        if features & (1 << bit))
    records = [(name, "_airplay._tcp", airplay)]
    if features & FEATURE_AUDIO:
        raop = {
            "ch": "2", "cn": "0,1,2,3", "da": "true",
            "et": "0,1,3,5" if legacy else "0,3,5", "vv": "2",
            "ft": airplay["features"], "am": info["model"], "md": metadata,
            "rhd": "5.6.0.0", "pw": "true" if require_pin else "false",
            "sf": "0x8c" if require_pin else "0x4",
            "sr": "44100", "ss": "16", "sv": "false", "tp": "UDP",
            "txtvers": "1", "vs": info["sourceVersion"], "vn": "65537",
            "pk": public_key.hex(),
        }
        records.append((f"{compact_device}@{name}", "_raop._tcp", raop))
    for _, _, txt in records:
        if any(not 1 <= len(f"{key}={value}".encode("utf-8")) <= 255
               for key, value in txt.items()):
            raise ValueError("TXT entry exceeds DNS length limit")
    return f"switch-tv-box-eden-{compact_device.lower()}.local", records


def read_records(args):
    info = get_info(args.address, args.port)
    airplay = decode_txt(get_info(args.address, args.port, True).get("txtAirPlay"))
    return service_records(info, airplay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", required=True, help="Physical LAN interface")
    parser.add_argument("--address", required=True, help="IPv4 address on that interface")
    parser.add_argument("--port", required=True, type=int, help="NRO control listener")
    parser.add_argument("--pid", required=True, type=int, help="Eden process owning listener")
    parser.add_argument("--check", action="store_true", help="Validate and print live records only")
    args = parser.parse_args()
    args.address = str(ipaddress.IPv4Address(args.address))
    if args.pid <= 0 or not 1024 <= args.port <= 65535:
        parser.error("Invalid process ID or control port")
    interface = socket.if_nametoindex(args.interface)
    if interface_address(args.interface) != args.address:
        parser.error("--address must match the selected interface's IPv4 address")
    identity = process_identity(args.pid)
    initial_listener = listener_identity(args.pid, args.address, args.port)
    if initial_listener is None:
        parser.error("The selected process does not own the control listener")
    initial = read_records(args)
    if args.check:
        print(json.dumps({"address": args.address, "port": args.port,
                          "host": initial[0], "services": initial[1]}, indent=2))
        return 0

    import dbus
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    server = dbus.Interface(bus.get_object("org.freedesktop.Avahi", "/"),
                            "org.freedesktop.Avahi.Server")
    group = dbus.Interface(bus.get_object("org.freedesktop.Avahi", server.EntryGroupNew()),
                           "org.freedesktop.Avahi.EntryGroup")
    loop = GLib.MainLoop()
    published = None
    published_listener = None
    last_probe = 0.0
    failed = False
    last_error = None

    def state_changed(state, error):
        nonlocal failed
        if state in (3, 4):  # Collision or failure: never rename another service.
            failed = True
            print(f"Discovery registration failed: {error}", flush=True)
            loop.quit()

    group.connect_to_signal("StateChanged", state_changed)

    def withdraw():
        nonlocal published, published_listener
        if published is not None:
            group.Reset()
            published = None
            published_listener = None
            print("Discovery withdrawn", flush=True)

    def tick():
        nonlocal published, published_listener, last_error, last_probe
        try:
            if process_identity(args.pid) != identity:
                loop.quit()
                return False
            current_listener = listener_identity(args.pid, args.address, args.port)
            if current_listener is None:
                withdraw()
                return True
            # Ownership is cheap to inspect every three seconds. A fresh
            # /info connection allocates an NRO worker thread, so refresh
            # unchanged capabilities only every thirty seconds.
            if (published is not None and current_listener == published_listener
                    and time.monotonic() - last_probe < 30):
                return True
            current = read_records(args)
            # A listener may disappear or its PID may be recycled during probes.
            if (process_identity(args.pid) != identity
                    or listener_identity(args.pid, args.address, args.port)
                    != current_listener):
                withdraw()
                return True
            if current != published:
                withdraw()
                host, records = current
                # NO_REVERSE avoids conflicting with existing host reverse PTRs.
                group.AddAddress(interface, 0, dbus.UInt32(16), host, args.address)
                for name, kind, txt in records:
                    group.AddService(
                        interface, 0, dbus.UInt32(0), name, kind, "", host,
                        dbus.UInt16(args.port),
                        [dbus.ByteArray(f"{key}={value}".encode("utf-8"))
                         for key, value in txt.items()])
                group.Commit()
                published = current
                print(f"Discovery: {records[0][0]} at {args.address}:{args.port} "
                      f"on {args.interface}, Eden PID {args.pid}", flush=True)
            published_listener = current_listener
            last_probe = time.monotonic()
            last_error = None
        except FileNotFoundError:
            if not Path(f"/proc/{args.pid}").exists():
                loop.quit()
                return False
            withdraw()
        except Exception as error:
            withdraw()
            if str(error) != last_error:
                print(f"Discovery unavailable: {error}", flush=True)
                last_error = str(error)
        return True

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: loop.quit())
    try:
        if tick():
            GLib.timeout_add_seconds(3, tick)
            loop.run()
    finally:
        group.Free()
        bus.close()
        print("Discovery stopped; this helper's records removed", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
