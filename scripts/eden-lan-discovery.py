#!/usr/bin/env python3
"""Automatically announce a live Switch TV Box NRO on local physical adapters.

Eden stubs IP multicast socket options. This host-side component implements
SSDP and mDNS using the host network stack. It never receives media or SOAP:
the description points those requests directly at the verified Eden listener.
"""

import argparse
import copy
from dataclasses import dataclass
from email.utils import formatdate
import heapq
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import os
from pathlib import Path
import random
import re
import select
import signal
import socket
import subprocess
import sys
import threading
import time
from urllib.parse import urljoin, urlsplit
import uuid
import xml.etree.ElementTree as ET

import psutil

from eden_receiver import decode_txt, get_info, service_records

GROUP = "239.255.255.250"
SSDP_PORT = 1900
DESCRIPTION_PORT = 18958
NS = "{urn:schemas-upnp-org:device-1-0}"
RENDERER = "urn:schemas-upnp-org:device:MediaRenderer:1"
SERVICE_PREFIX = "urn:schemas-upnp-org:service:"
MAX_XML = 65536
VIRTUAL_NAMES = re.compile(
    r"loopback|docker|veth|virbr|vmware|virtualbox|hyper-v|vethernet|"
    r"zerotier|tailscale|wireguard|\btun\b|\btap\b|\bmeta\b|vpn|wsl", re.I)


def say(message):
    print(time.strftime("[%H:%M:%S] ") + message, flush=True)


def usable_address(address):
    try:
        ip = ipaddress.IPv4Address(address)
        return not (ip.is_loopback or ip.is_unspecified or ip.is_multicast
                    or ip.is_link_local or ip.is_reserved)
    except ipaddress.AddressValueError:
        return False


@dataclass(frozen=True)
class Adapter:
    name: str
    address: str
    netmask: str


def physical_names():
    """Ask Windows for physical adapters; no network configuration is changed."""
    if sys.platform == "win32":
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
             "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding; "
             "@(Get-NetAdapter -Physical -ErrorAction Stop | "
             "Where-Object Status -eq 'Up' | Select-Object -ExpandProperty Name) | "
             "ConvertTo-Json -Compress"],
            capture_output=True, timeout=5, encoding="utf-8", errors="replace",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if result.returncode == 0:
            values = json.loads(result.stdout or "[]")
            return set([values] if isinstance(values, str) else (values or []))
        raise RuntimeError("Windows could not enumerate physical network adapters")
    if sys.platform.startswith("linux"):
        return {path.name for path in Path("/sys/class/net").iterdir()
                if (path / "device").exists()}
    return None


def select_adapters(addresses, statistics, physical=None):
    candidates = []
    for name, entries in addresses.items():
        status = statistics.get(name)
        if not status or not status.isup:
            continue
        if physical is not None and name not in physical:
            continue
        if physical is None and VIRTUAL_NAMES.search(name):
            continue
        for entry in entries:
            if entry.family == socket.AF_INET and usable_address(entry.address):
                candidates.append(Adapter(name, entry.address, entry.netmask or "255.255.255.255"))
    # Ethernet and Wi-Fi on the same subnet need only one announcement.
    selected, networks = [], set()
    for item in sorted(candidates, key=lambda value: (value.name.casefold(), value.address)):
        network = ipaddress.IPv4Interface(f"{item.address}/{item.netmask}").network
        if network not in networks:
            networks.add(network)
            selected.append(item)
    return tuple(selected)


def connections(process):
    method = getattr(process, "net_connections", None) or process.connections
    return method(kind="tcp4")


def listener_ports(process):
    return frozenset(connection.laddr.port for connection in connections(process)
                     if connection.status == psutil.CONN_LISTEN and connection.laddr
                     and connection.laddr.port in (7000, 49152))


def find_receiver(requested_pid=None):
    found = []
    for process in psutil.process_iter(["pid", "name", "create_time"]):
        if requested_pid is not None and process.pid != requested_pid:
            continue
        if (process.info["name"] or "").lower() not in ("eden", "eden.exe", "eden-cli", "eden-cli.exe"):
            continue
        try:
            ports = listener_ports(process)
            if ports:
                found.append((process.pid, process.create_time(), ports))
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    if len(found) > 1:
        raise RuntimeError("More than one Eden receiver is running; close the extra session")
    return found[0] if found else None


def still_owned(identity):
    if identity is None:
        return False
    try:
        process = psutil.Process(identity[0])
        return process.create_time() == identity[1] and listener_ports(process) == identity[2]
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return False


def parse_description(body):
    if not body or len(body) > MAX_XML or b"<!DOCTYPE" in body.upper() or b"<!ENTITY" in body.upper():
        raise ValueError("Invalid DLNA description")
    root = ET.fromstring(body)
    device = root.find(NS + "device")
    if root.tag != NS + "root" or device is None or device.findtext(NS + "deviceType") != RENDERER:
        raise ValueError("Receiver is not a DLNA MediaRenderer")
    udn = device.findtext(NS + "UDN", "").strip()
    if not udn.startswith("uuid:"):
        raise ValueError("DLNA receiver has no UUID")
    uuid.UUID(udn[5:])
    name = device.findtext(NS + "friendlyName", "").strip()
    if not name.startswith("Switch TV Box"):
        raise ValueError("DLNA listener is not Switch TV Box")
    services = tuple(item.text for item in device.findall(
        NS + "serviceList/" + NS + "service/" + NS + "serviceType")
        if item.text in tuple(SERVICE_PREFIX + value + ":1" for value in
                             ("AVTransport", "RenderingControl", "ConnectionManager")))
    if SERVICE_PREFIX + "AVTransport:1" not in services:
        raise ValueError("DLNA receiver has no AVTransport service")
    records = [("upnp:rootdevice", udn + "::upnp:rootdevice"), (udn, udn),
               (RENDERER, udn + "::" + RENDERER)]
    records.extend((service, udn + "::" + service) for service in dict.fromkeys(services))
    return root, tuple(records)


def read_description(address):
    connection = http.client.HTTPConnection(address, 49152, timeout=2)
    try:
        connection.request("GET", "/description.xml", headers={"Connection": "close"})
        response = connection.getresponse()
        if response.status != 200:
            raise ValueError(f"DLNA description returned HTTP {response.status}")
        return parse_description(response.read(MAX_XML + 1))
    finally:
        connection.close()


def description_for_interface(root, address):
    """Resolve every control/resource URL to the actual reachable NRO address."""
    document = copy.deepcopy(root)
    base = f"http://{address}:49152/"
    url_base = document.find(NS + "URLBase")
    if url_base is None:
        url_base = ET.SubElement(document, NS + "URLBase")
    url_base.text = base
    for element in document.iter():
        if element.tag in {NS + name for name in
                           ("controlURL", "eventSubURL", "SCPDURL", "presentationURL", "url")}:
            old = urlsplit(element.text or "")
            path = old.path + (("?" + old.query) if old.query else "")
            element.text = urljoin(base, path)
    return ET.tostring(document, encoding="utf-8", xml_declaration=True)


def search_target(data):
    if len(data) > 4096 or not data.endswith(b"\r\n\r\n"):
        return None
    try:
        lines = data.decode("ascii").split("\r\n")
        if lines[0] != "M-SEARCH * HTTP/1.1":
            return None
        headers = {}
        for line in lines[1:]:
            if not line:
                continue
            key, colon, value = line.partition(":")
            key = key.strip().lower()
            if not colon or key in headers:
                return None
            headers[key] = value.strip()
        if headers.get("man", "").lower() != '"ssdp:discover"':
            return None
        delay = int(headers.get("mx", "0"))
        if not 1 <= delay <= 5 or len(headers.get("st", "")) > 256:
            return None
        return headers.get("st", ""), min(delay, 2)
    except (UnicodeDecodeError, ValueError):
        return None


def ssdp_packet(address, target, usn, notification=None):
    lines = (["NOTIFY * HTTP/1.1", f"HOST: {GROUP}:{SSDP_PORT}",
              f"NT: {target}", f"NTS: ssdp:{notification}"] if notification else
             ["HTTP/1.1 200 OK", f"ST: {target}", "EXT:"])
    lines += [f"USN: {usn}"]
    if notification != "byebye":
        lines += ["CACHE-CONTROL: max-age=66", f"DATE: {formatdate(usegmt=True)}",
                  f"LOCATION: http://{address}:{DESCRIPTION_PORT}/description.xml",
                  "SERVER: Windows-Eden/1.0 UPnP/1.0 Switch-TV-Box/1.0"]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("ascii")


class SsdpPublisher:
    def __init__(self, adapter, records):
        self.adapter, self.records = adapter, records
        self.pending, self.recent = [], {}
        self.rx = self.tx = None
        try:
            self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.rx.bind(("", SSDP_PORT))
            self.rx.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                               socket.inet_aton(GROUP) + socket.inet_aton(adapter.address))
            if sys.platform.startswith("linux"):
                self.rx.setsockopt(socket.IPPROTO_IP, 49, 0)  # IP_MULTICAST_ALL
            self.rx.setblocking(False)
            self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.tx.bind((adapter.address, 0))
            self.tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                               socket.inet_aton(adapter.address))
            self.tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            self.tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
            self.announce("alive")
            self.next_announcement = time.monotonic() + 1
        except Exception:
            self.close(announce=False)
            raise

    def announce(self, kind):
        for target, usn in self.records:
            self.tx.sendto(ssdp_packet(self.adapter.address, target, usn, kind), (GROUP, SSDP_PORT))

    def receive(self):
        data, sender = self.rx.recvfrom(4097)
        query = search_target(data)
        if query is None or not usable_address(sender[0]) or sender[1] == 0:
            return
        target, delay = query
        now = time.monotonic()
        self.recent = {key: value for key, value in self.recent.items() if now - value < 1}
        key = (sender, target)
        if key in self.recent or len(self.recent) >= 64:
            return
        self.recent[key] = now
        for kind, usn in self.records:
            if target not in ("ssdp:all", kind) or len(self.pending) >= 128:
                continue
            heapq.heappush(self.pending, (now + random.uniform(0, delay), sender,
                                         ssdp_packet(self.adapter.address, kind, usn)))

    def tick(self):
        now = time.monotonic()
        while self.pending and self.pending[0][0] <= now:
            _, sender, packet = heapq.heappop(self.pending)
            self.tx.sendto(packet, sender)
        if now >= self.next_announcement:
            self.announce("alive")
            self.next_announcement = now + 30

    def close(self, announce=True):
        if self.tx:
            if announce:
                try:
                    self.announce("byebye")
                except OSError:
                    pass
            self.tx.close()
            self.tx = None
        if self.rx:
            self.rx.close()
            self.rx = None


class MdnsPublisher:
    def __init__(self, adapter, records):
        from zeroconf import IPVersion, ServiceInfo, Zeroconf
        self.zc = Zeroconf(interfaces=[adapter.address], ip_version=IPVersion.V4Only)
        self.services = []
        host, services = records
        try:
            for name, kind, txt in services:
                type_name = kind + ".local."
                service = ServiceInfo(type_name, name + "." + type_name,
                                      addresses=[socket.inet_aton(adapter.address)], port=7000,
                                      properties=txt, server=host + ".")
                self.zc.register_service(service, allow_name_change=False)
                self.services.append(service)
        except Exception:
            self.close()
            raise

    def close(self):
        try:
            self.zc.unregister_all_services()
        finally:
            self.zc.close()


class DescriptionHandler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def log_message(self, *_):
        pass

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_GET(self, head=False):
        manager = self.server.manager
        address = self.connection.getsockname()[0]
        if self.path != "/description.xml" or address not in manager.addresses:
            self.send_error(404)
            return
        root = manager.description
        if root is None or not still_owned(manager.identity):
            self.send_error(503, "Eden receiver is not running")
            return
        body = description_for_interface(root, address)
        self.send_response(200)
        self.send_header("Content-Type", "text/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        if not head:
            self.wfile.write(body)


class DiscoveryManager:
    def __init__(self, requested_pid=None, only_address=None, ssdp_only=False):
        self.requested_pid, self.only_address, self.ssdp_only = requested_pid, only_address, ssdp_only
        self.identity = self.description = None
        self.addresses = set()
        self.publishers = []
        self.http = None
        self.last_probe = 0
        self.last_key = None
        self.last_message = None

    def status(self, message):
        if message != self.last_message:
            say(message)
            self.last_message = message

    def withdraw(self):
        self.identity = self.description = None
        self.addresses = set()
        for publisher in self.publishers:
            try:
                publisher.close()
            except Exception as error:
                say(f"Discovery cleanup: {error}")
        self.publishers = []
        if self.http:
            self.http.shutdown()
            self.http.server_close()
            self.http = None
        self.last_key = None

    def refresh(self):
        identity = find_receiver(self.requested_pid)
        adapters = select_adapters(psutil.net_if_addrs(), psutil.net_if_stats(), physical_names())
        if self.only_address:
            adapters = tuple(item for item in adapters if item.address == self.only_address)
        key = (identity, adapters)
        if not identity or not adapters:
            self.withdraw()
            self.status("Waiting for Eden running Switch TV Box and an active LAN adapter...")
            return
        if key == self.last_key and time.monotonic() - self.last_probe < 30:
            return
        # Probes use a local destination, bypass environment/system HTTP proxies.
        probe_ip = adapters[0].address
        airplay = dlna = None
        if 7000 in identity[2] and not self.ssdp_only:
            info = get_info(probe_ip, 7000)
            txt = decode_txt(get_info(probe_ip, 7000, True).get("txtAirPlay"))
            airplay = service_records(info, txt)
        if 49152 in identity[2]:
            dlna = read_description(probe_ip)
        if not still_owned(identity):
            self.withdraw()
            return
        self.last_probe = time.monotonic()
        # Identity changes require withdrawing stale TXT/UUIDs too.
        fingerprint = (key, airplay, ET.tostring(dlna[0]) if dlna else None)
        if getattr(self, "fingerprint", None) == fingerprint and self.publishers:
            self.last_key = key
            return
        self.withdraw()
        self.identity = identity
        self.addresses = {item.address for item in adapters}
        if dlna:
            self.description = dlna[0]
            self.http = ThreadingHTTPServer(("0.0.0.0", DESCRIPTION_PORT), DescriptionHandler)
            self.http.daemon_threads = True
            self.http.manager = self
            threading.Thread(target=self.http.serve_forever, daemon=True).start()
        try:
            for adapter in adapters:
                if dlna:
                    self.publishers.append(SsdpPublisher(adapter, dlna[1]))
                if airplay:
                    self.publishers.append(MdnsPublisher(adapter, airplay))
        except Exception:
            self.withdraw()
            raise
        self.last_key, self.fingerprint = key, fingerprint
        self.status("Automatic discovery active: " + ", ".join(
            f"{item.name} ({item.address})" for item in adapters) +
            f"; AirPlay={'yes' if airplay else 'no'}, DLNA={'yes' if dlna else 'no'}")

    def poll(self):
        publishers = [item for item in self.publishers if isinstance(item, SsdpPublisher)]
        if publishers:
            ready, _, _ = select.select([item.rx for item in publishers], [], [], 0.2)
            for item in publishers:
                if item.rx in ready:
                    try:
                        item.receive()
                    except BlockingIOError:
                        pass
                item.tick()
        else:
            time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, help="Optional Eden PID; normally detected automatically")
    parser.add_argument("--interface-ip", help="Optional diagnostic interface restriction")
    parser.add_argument("--ssdp-only", action="store_true", help="For an existing Linux Avahi launcher")
    parser.add_argument("--duration", type=float, default=0, help="Diagnostic run duration; zero waits for Ctrl+C")
    parser.add_argument("--self-check", action="store_true", help="Check packaged modules and process inspection")
    args = parser.parse_args()
    if args.self_check:
        import zeroconf
        connections(psutil.Process())
        print(json.dumps({"platform": sys.platform, "psutil": psutil.__version__,
                          "zeroconf": zeroconf.__version__, "process_inspection": True}))
        return 0
    stopped = False

    def stop(*_):
        nonlocal stopped
        stopped = True

    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, stop)
    # One component per machine; no system configuration or firewall changes.
    with socket.socket() as singleton:
        singleton.bind(("127.0.0.1", 18959))
        singleton.listen(1)
        manager = DiscoveryManager(args.pid, args.interface_ip, args.ssdp_only)
        started, next_refresh = time.monotonic(), 0
        say("Switch TV Box automatic LAN discovery. Start the NRO in Eden; keep this window open.")
        try:
            while not stopped and (not args.duration or time.monotonic() - started < args.duration):
                try:
                    if time.monotonic() >= next_refresh:
                        manager.refresh()
                        next_refresh = time.monotonic() + 3
                    manager.poll()
                except Exception as error:
                    manager.withdraw()
                    manager.status(f"Waiting to retry: {error}")
                    next_refresh = time.monotonic() + 3
        finally:
            manager.withdraw()
            say("Discovery stopped; announcements withdrawn.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"Cannot start discovery: {error}", file=sys.stderr)
        raise SystemExit(1)
