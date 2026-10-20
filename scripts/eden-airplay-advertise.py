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
import re
import signal
import socket
import struct
import time


from eden_receiver import (FEATURE_AUDIO, FEATURE_RSA, MAX_REPLY, get_info,
                           decode_txt, feature_bits, service_records)


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
