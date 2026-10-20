#!/usr/bin/env python3
"""Optional Eden HTTP relay: public IPv4 literals only, through one interface.

This is deliberately not a general HTTP/HTTPS proxy. It performs no DNS lookup,
URL rewriting, redirect following, caching, or request/token logging.
"""

import argparse
import contextlib
import ipaddress
import os
import re
import signal
import socket
import threading
from urllib.parse import urlsplit


ADDRESS = "127.0.0.1"
PORT = 18957
MAX_CLIENTS = 8
HEADER_LIMIT = 16 * 1024
CONNECT_TIMEOUT = 5
IDLE_TIMEOUT = 90
BLOCKED_NETWORKS = tuple(ipaddress.IPv4Network(value) for value in (
    "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8",
    "169.254.0.0/16", "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24",
    "192.88.99.0/24", "192.168.0.0/16", "198.18.0.0/15",
    "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/4", "240.0.0.0/4",
))
HEADER_NAME = re.compile(rb"^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$")


class RequestError(Exception):
    def __init__(self, status, reason):
        self.status = status
        self.reason = reason


def public_target(url):
    """Return a validated numeric endpoint and unchanged origin-form target."""
    if any(ord(char) <= 32 or ord(char) >= 127 for char in url):
        raise RequestError(400, "Invalid HTTP request target")
    try:
        parsed = urlsplit(url)
        if parsed.scheme != "http":
            raise RequestError(501, "Only HTTP public IPv4 targets are supported")
        if parsed.username is not None or parsed.password is not None or "#" in url:
            raise RequestError(400, "Userinfo and fragments are not supported")
        # Validate the authority itself, not only urlsplit's normalized hostname.
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}(?::[0-9]+)?", parsed.netloc):
            raise RequestError(501, "Only HTTP public IPv4 targets are supported")
        address = ipaddress.IPv4Address(parsed.hostname)
        port = parsed.port if parsed.port is not None else 80
        if not 1 <= port <= 65535:
            raise ValueError("port")
    except ValueError as error:
        raise RequestError(400, "Invalid IPv4 address or port") from error
    if any(address in network for network in BLOCKED_NETWORKS):
        raise RequestError(403, "Only public IPv4 destinations are allowed")
    # Preserve the query exactly, including percent escapes and a trailing '?'.
    target = url[len("http://") + len(parsed.netloc):] or "/"
    if target.startswith("?"):
        target = "/" + target
    return str(address), port, parsed.netloc, target


def read_request(client):
    data = bytearray()
    while b"\r\n\r\n" not in data:
        remaining = HEADER_LIMIT - len(data)
        if remaining <= 0:
            raise RequestError(431, "Request headers exceed 16 KiB")
        part = client.recv(min(4096, remaining))
        if not part:
            raise RequestError(400, "Incomplete request headers")
        data.extend(part)
    header, extra = bytes(data).split(b"\r\n\r\n", 1)
    if extra:
        raise RequestError(400, "Request bodies and pipelining are not supported")
    lines = header.split(b"\r\n")
    request = lines[0].split(b" ")
    if len(request) != 3 or request[2] not in (b"HTTP/1.0", b"HTTP/1.1"):
        raise RequestError(400, "Invalid HTTP request line")
    method, raw_url, _ = request
    if method not in (b"GET", b"HEAD"):
        raise RequestError(405, "Only GET and HEAD are supported")
    try:
        endpoint, port, authority, target = public_target(raw_url.decode("ascii"))
    except UnicodeDecodeError as error:
        raise RequestError(400, "Invalid HTTP request target") from error

    headers = []
    connection_names = set()
    seen = set()
    for line in lines[1:]:
        name, separator, value = line.partition(b":")
        if not separator or not HEADER_NAME.fullmatch(name):
            raise RequestError(400, "Invalid HTTP header")
        if any(byte < 32 and byte != 9 or byte == 127 for byte in value):
            raise RequestError(400, "Invalid HTTP header value")
        lower = name.lower()
        if lower in (b"host", b"content-length", b"transfer-encoding"):
            if lower in seen:
                raise RequestError(400, "Duplicate framing header")
            seen.add(lower)
        if lower == b"transfer-encoding" or (
            lower == b"content-length" and value.strip() != b"0"
        ):
            raise RequestError(400, "Request bodies are not supported")
        if lower == b"expect":
            raise RequestError(400, "Expect is not supported")
        if lower == b"connection":
            tokens = [token.strip().lower() for token in value.split(b",")]
            if any(not HEADER_NAME.fullmatch(token) for token in tokens):
                raise RequestError(400, "Invalid Connection header")
            connection_names.update(tokens)
        headers.append((lower, line))
    skipped = connection_names | {
        b"host", b"connection", b"proxy-connection", b"proxy-authorization",
        b"content-length", b"keep-alive", b"te", b"trailer", b"upgrade",
    }
    # One request per connection keeps persistent proxy requests from reaching
    # another destination through an already connected upstream socket.
    outgoing = (
        method + b" " + target.encode("ascii") + b" HTTP/1.1\r\nHost: "
        + authority.encode("ascii") + b"\r\nConnection: close\r\n"
        + b"".join(line + b"\r\n" for name, line in headers if name not in skipped)
        + b"\r\n"
    )
    return endpoint, port, outgoing


def bound_socket(interface):
    encoded = interface.encode("ascii")
    if not encoded or len(encoded) >= 16 or b"\0" in encoded:
        raise ValueError("Invalid network interface")
    socket.if_nametoindex(interface)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, encoded + b"\0")
        return sock
    except BaseException:
        sock.close()
        raise


def connect_bound(interface, address, port):
    sock = bound_socket(interface)
    try:
        sock.settimeout(CONNECT_TIMEOUT)
        # Numeric address was validated before this call; no DNS or environment
        # HTTP_PROXY/ALL_PROXY settings are consulted.
        sock.connect((address, port))
        sock.settimeout(IDLE_TIMEOUT)
        return sock
    except BaseException:
        sock.close()
        raise


def send_error(client, status, reason):
    body = (reason + "\n").encode("ascii")
    response = (
        f"HTTP/1.1 {status} {reason}\r\n"
        f"Content-Length: {len(body)}\r\nContent-Type: text/plain\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii") + body
    with contextlib.suppress(OSError):
        client.settimeout(1)
        client.sendall(response)


class DirectHTTPServer:
    def __init__(self, interface, *, connector=None, port=PORT):
        self.interface = interface
        self.connector = connector or connect_bound
        self.stopped = threading.Event()
        self.slots = threading.BoundedSemaphore(MAX_CLIENTS)
        self.lock = threading.Lock()
        self.sockets = set()
        self.workers = set()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.listener.bind((ADDRESS, port))
            self.listener.listen(MAX_CLIENTS)
            self.listener.settimeout(0.25)
        except BaseException:
            self.listener.close()
            raise
        self.port = self.listener.getsockname()[1]

    def handle(self, client):
        upstream = None
        response_started = False
        try:
            client.settimeout(IDLE_TIMEOUT)
            address, port, outgoing = read_request(client)
            upstream = self.connector(self.interface, address, port)
            with self.lock:
                self.sockets.add(upstream)
            if self.stopped.is_set():
                return
            upstream.settimeout(IDLE_TIMEOUT)
            upstream.sendall(outgoing)
            while not self.stopped.is_set():
                data = upstream.recv(64 * 1024)
                if not data:
                    if not response_started:
                        raise OSError("Empty upstream response")
                    break
                response_started = True
                client.sendall(data)
        except RequestError as error:
            send_error(client, error.status, error.reason)
        except (OSError, ValueError):
            # Never append a synthetic status to a partly forwarded response,
            # and never print exception text containing a request URL or token.
            if not response_started and not self.stopped.is_set():
                send_error(client, 502, "Direct HTTP upstream unavailable")
        finally:
            for sock in (client, upstream):
                if sock is not None:
                    with self.lock:
                        self.sockets.discard(sock)
                    sock.close()
            with self.lock:
                self.workers.discard(threading.current_thread())
            self.slots.release()

    def serve(self, parent_pid=None):
        while not self.stopped.is_set():
            if parent_pid is not None and os.getppid() != parent_pid:
                break
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                if self.stopped.is_set():
                    break
                raise
            if not self.slots.acquire(blocking=False):
                send_error(client, 503, "Direct HTTP connection limit reached")
                client.close()
                continue
            worker = threading.Thread(target=self.handle, args=(client,), daemon=True)
            with self.lock:
                self.sockets.add(client)
                self.workers.add(worker)
            try:
                worker.start()
            except RuntimeError:
                with self.lock:
                    self.sockets.discard(client)
                    self.workers.discard(worker)
                client.close()
                self.slots.release()

    def close(self):
        self.stopped.set()
        self.listener.close()
        with self.lock:
            sockets, workers = tuple(self.sockets), tuple(self.workers)
        for sock in sockets:
            with contextlib.suppress(OSError):
                sock.shutdown(socket.SHUT_RDWR)
        for worker in workers:
            worker.join(timeout=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", required=True)
    parser.add_argument("--parent-pid", type=int)
    parser.add_argument("--ready-fd", type=int)
    parser.add_argument("--check", action="store_true",
                        help="check interface binding and listener availability, then exit")
    args = parser.parse_args()
    if args.parent_pid is not None and args.parent_pid != os.getppid():
        parser.error("--parent-pid must identify this helper's launcher")
    with bound_socket(args.interface):
        pass
    server = DirectHTTPServer(args.interface)
    try:
        if args.check:
            print("Direct HTTP preflight passed; no relay left running.")
            return 0
        for number in (signal.SIGINT, signal.SIGTERM):
            signal.signal(number, lambda _number, _frame: server.stopped.set())
        if args.ready_fd is not None:
            os.write(args.ready_fd, b"R")
            os.close(args.ready_fd)
        print(f"Direct HTTP ready on {ADDRESS}:{PORT}; interface {args.interface}; "
              "public IPv4 HTTP GET/HEAD only", flush=True)
        server.serve(args.parent_pid)
        return 0
    finally:
        server.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        # No request is handled on this path. Avoid environment/proxy dumps.
        print(f"Direct HTTP startup failed ({type(error).__name__}).", flush=True)
        raise SystemExit(1)
