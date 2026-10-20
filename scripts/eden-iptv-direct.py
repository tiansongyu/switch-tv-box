#!/usr/bin/env python3
"""Optional Eden HTTP relay: public IPv4 literals only, through one interface.

This is deliberately not a general HTTP/HTTPS proxy. It performs no DNS lookup,
URL rewriting, redirect following, caching, or request/token logging.
"""

import argparse
from collections import deque
import contextlib
import ipaddress
import json
import os
import re
import signal
import socket
import threading
import time
from urllib.parse import urlsplit


ADDRESS = "127.0.0.1"
PORT = 18957
MAX_CLIENTS = 8
HEADER_LIMIT = 16 * 1024
CONNECT_TIMEOUT = 5
IDLE_TIMEOUT = 90
DIAGNOSTIC_REQUEST_LIMIT = 1024
RESUME_MIN_BYTES = 256 * 1024
RESUME_MAX_ATTEMPTS = 2
SLOW_WINDOW_SECONDS = 8
SLOW_BYTES_PER_SECOND = 128 * 1024
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


class ResponseError(Exception):
    """A fixed diagnostic reason, never an upstream header or URL."""


class ChunkBoundary:
    """Locate the chunked message boundary without decoding or changing bytes."""
    def __init__(self):
        self.state = "size"
        self.line = bytearray()
        self.remaining = 0
        self.trailer_bytes = 0
        self.complete = False

    def consume(self, data):
        index = 0
        while index < len(data) and not self.complete:
            if self.state == "data":
                count = min(self.remaining, len(data) - index)
                self.remaining -= count
                index += count
                if not self.remaining:
                    self.state = "data-end"
                continue
            self.line.append(data[index])
            index += 1
            if self.state == "trailer":
                self.trailer_bytes += 1
                if self.trailer_bytes > HEADER_LIMIT:
                    raise ResponseError("invalid-chunk-trailer")
            if len(self.line) > HEADER_LIMIT:
                raise ResponseError("invalid-chunk-line")
            if not self.line.endswith(b"\r\n"):
                continue
            line = bytes(self.line[:-2])
            self.line.clear()
            if self.state == "size":
                size = line.split(b";", 1)[0].strip()
                if not re.fullmatch(rb"[0-9A-Fa-f]{1,16}", size):
                    raise ResponseError("invalid-chunk-size")
                self.remaining = int(size, 16)
                self.state = "data" if self.remaining else "trailer"
            elif self.state == "data-end":
                if line:
                    raise ResponseError("invalid-chunk-delimiter")
                self.state = "size"
            elif not line:
                self.complete = True
            elif not HEADER_NAME.fullmatch(line.partition(b":")[0]) or b":" not in line:
                raise ResponseError("invalid-chunk-trailer")
        return index


class HTTPResponse:
    """Frame a single response, preserving media bytes and closing downstream."""
    def __init__(self, head=False):
        self.head = head
        self.headers = bytearray()
        self.status = None
        self.mode = None
        self.content_length = None
        self.body_bytes = 0
        self.complete = False
        self.chunk = None
        self.informational = 0
        self.fields = {}
        self.last_body = b""

    def feed(self, data):
        self.last_body = b""
        output = bytearray()
        while self.mode is None:
            self.headers.extend(data)
            end = self.headers.find(b"\r\n\r\n")
            if end < 0:
                if len(self.headers) >= HEADER_LIMIT:
                    raise ResponseError("response-header-limit")
                return bytes(output)
            if end + 4 > HEADER_LIMIT:
                raise ResponseError("response-header-limit")
            header, data = bytes(self.headers[:end]), bytes(self.headers[end + 4:])
            self.headers.clear()
            lines = header.split(b"\r\n")
            match = re.fullmatch(rb"HTTP/1\.[01] ([0-9]{3})(?: [^\r\n]*)?", lines[0])
            if not match:
                raise ResponseError("invalid-response-status")
            status = int(match[1])
            if not 100 <= status <= 599 or status == 101:
                raise ResponseError("unsupported-response-status")
            kept = []
            lengths, transfers = [], []
            fields = {}
            for line in lines[1:]:
                name, separator, value = line.partition(b":")
                if not separator or not HEADER_NAME.fullmatch(name) or any(
                    byte < 32 and byte != 9 or byte == 127 for byte in value
                ):
                    raise ResponseError("invalid-response-header")
                name = name.lower()
                fields.setdefault(name, []).append(value.strip())
                if name == b"content-length":
                    lengths.append(value.strip())
                if name == b"transfer-encoding":
                    transfers.extend(item.strip().lower() for item in value.split(b","))
                if name not in (b"connection", b"proxy-connection", b"keep-alive"):
                    kept.append(line)
            if status < 200:
                self.informational += 1
                if self.informational > 8:
                    raise ResponseError("too-many-informational-responses")
                output.extend(header + b"\r\n\r\n")
                continue
            self.status = status
            self.fields = fields
            if lengths:
                if len(lengths) != 1 or not re.fullmatch(rb"[0-9]{1,20}", lengths[0]):
                    raise ResponseError("invalid-content-length")
                self.content_length = int(lengths[0])
            if lengths and transfers:
                raise ResponseError("ambiguous-response-framing")
            if self.head or status in (204, 304):
                self.mode = "no-body"
                self.complete = True
            elif transfers and transfers[-1] == b"chunked":
                self.mode = "chunked"
                self.chunk = ChunkBoundary()
            elif transfers:
                self.mode = "close"
            elif self.content_length is not None:
                self.mode = "content-length"
                self.complete = self.content_length == 0
            else:
                self.mode = "close"
            # A relay connection handles exactly one request. Do not advertise
            # upstream keep-alive, even if it ignores our Connection:close.
            output.extend(lines[0] + b"\r\n" + b"".join(line + b"\r\n" for line in kept)
                          + b"Connection: close\r\n\r\n")
        count = 0
        if not self.complete:
            if self.mode == "content-length":
                count = min(len(data), self.content_length - self.body_bytes)
                self.complete = self.body_bytes + count == self.content_length
            elif self.mode == "chunked":
                count = self.chunk.consume(data)
                self.complete = self.chunk.complete
            else:
                count = len(data)
            self.body_bytes += count
            self.last_body = data[:count]
            output.extend(self.last_body)
        return bytes(output)

    def eof(self):
        if self.mode == "close":
            self.complete = True
        elif not self.complete:
            raise ResponseError("upstream-truncated")


def single_field(response, name):
    values = response.fields.get(name, [])
    return values[0] if len(values) == 1 else None


def identity_response(response):
    return (b"content-encoding" not in response.fields
            or single_field(response, b"content-encoding") == b"identity")


def response_range(response):
    value = single_field(response, b"content-range") or b""
    match = re.fullmatch(rb"bytes ([0-9]{1,20})-([0-9]{1,20})/([0-9]{1,20})", value)
    if not match:
        return None
    start, end, total = map(int, match.groups())
    if not 0 <= start <= end < total or end - start + 1 != response.content_length:
        return None
    return start, end, total


class ResumePolicy:
    """Join only byte-identical, strongly validated ranges of one media entity."""
    def __init__(self, response):
        self.etag = single_field(response, b"etag")
        self.length = response.content_length
        self.media_type = single_field(response, b"content-type").split(b";")[0].lower()

    @classmethod
    def for_response(cls, response, request):
        if (not request.startswith(b"GET ") or response.mode != "content-length"
                or response.content_length < RESUME_MIN_BYTES
                or not identity_response(response)):
            return None
        etag = single_field(response, b"etag") or b""
        if not re.fullmatch(rb'"[\x21\x23-\x7e\x80-\xff]*"', etag):
            return None
        media_type = (single_field(response, b"content-type") or b"").split(b";")[0].lower()
        if not (media_type.startswith((b"video/", b"audio/"))
                or media_type in (b"application/octet-stream", b"application/mp4")):
            return None
        if response.status == 206:
            # A correct full-entity 206 itself proves byte-range support.
            if response_range(response) != (0, response.content_length - 1,
                                            response.content_length):
                return None
        elif (response.status != 200 or single_field(response, b"accept-ranges") != b"bytes"
              or b"content-range" in response.fields):
            return None
        return cls(response)

    def request(self, original, delivered):
        lines = original.split(b"\r\n")
        kept = [line for line in lines[1:] if line and line.partition(b":")[0].lower()
                not in (b"range", b"if-range", b"accept-encoding")]
        return (lines[0] + b"\r\n" + b"".join(line + b"\r\n" for line in kept)
                + f"Range: bytes={delivered}-{self.length - 1}\r\n".encode()
                + b"If-Range: " + self.etag + b"\r\nAccept-Encoding: identity\r\n\r\n")

    def validate(self, response, delivered):
        media_types = response.fields.get(b"content-type", [])
        if (response.status != 206 or response.mode != "content-length"
                or not identity_response(response)
                or single_field(response, b"etag") != self.etag
                or (media_types and (len(media_types) != 1
                    or media_types[0].split(b";")[0].lower() != self.media_type))
                or response_range(response) != (delivered, self.length - 1, self.length)):
            raise ResponseError("resume-response-mismatch")


class SlowTransfer:
    """An eight-second receive-wait window; downstream writes consume no time."""
    def __init__(self):
        self.wait = 0.0
        self.samples = deque()

    def observe(self, wait, count):
        self.wait += wait
        # Coalescing bounds storage even when the origin sends tiny TCP writes.
        bucket = int(self.wait * 4)
        if self.samples and self.samples[-1][0] == bucket:
            old = self.samples.pop()
            count += old[2]
        self.samples.append((bucket, self.wait, count))
        cutoff = self.wait - SLOW_WINDOW_SECONDS
        while self.samples and self.samples[0][1] <= cutoff:
            self.samples.popleft()

    def slow(self):
        return (self.wait >= SLOW_WINDOW_SECONDS
                and sum(sample[2] for sample in self.samples)
                < SLOW_WINDOW_SECONDS * SLOW_BYTES_PER_SECOND)


class DirectHTTPServer:
    def __init__(self, interface, *, connector=None, port=PORT,
                 diagnostics=False, diagnostic_sink=None):
        self.interface = interface
        self.connector = connector or connect_bound
        self.stopped = threading.Event()
        self.slots = threading.BoundedSemaphore(MAX_CLIENTS)
        self.lock = threading.Lock()
        self.sockets = set()
        self.workers = set()
        self.diagnostics = diagnostics
        self.diagnostic_sink = diagnostic_sink
        self.diagnostic_lock = threading.Lock()
        self.sequence = 0
        self.started = time.monotonic()
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

    def diagnostic(self, sequence, event, **fields):
        if not self.diagnostics or sequence > DIAGNOSTIC_REQUEST_LIMIT:
            return
        record = {
            "request": sequence, "event": event,
            "elapsed_ms": round((time.monotonic() - self.started) * 1000),
            **fields,
        }
        with self.diagnostic_lock:
            if self.diagnostic_sink is not None:
                self.diagnostic_sink(record)
            else:
                print(json.dumps(record, separators=(",", ":")), flush=True)

    def handle(self, client, sequence):
        upstream = None
        response_started = False
        response = None
        active_response = None
        resume_policy = None
        resume_attempts = 0
        resume_validated = True
        resume_header_deadline = None
        slow_transfer = None
        body_forwarded = 0
        started = time.monotonic()
        connect_ms = first_byte_ms = None
        received = forwarded = 0
        upstream_wait_ms = client_write_ms = 0.0
        status = None
        phase = "request-read"
        reason = "server-stop"
        error_number = None
        self.diagnostic(sequence, "accepted")
        try:
            client.settimeout(IDLE_TIMEOUT)
            address, port, outgoing = read_request(client)
            response = HTTPResponse(head=outgoing.startswith(b"HEAD "))
            active_response = response
            phase = "upstream-connect"
            before = time.monotonic()
            upstream = self.connector(self.interface, address, port)
            connect_ms = round((time.monotonic() - before) * 1000, 3)
            self.diagnostic(sequence, "connected", connect_ms=connect_ms)
            with self.lock:
                self.sockets.add(upstream)
            if self.stopped.is_set():
                return
            upstream.settimeout(IDLE_TIMEOUT)
            phase = "upstream-write"
            upstream.sendall(outgoing)
            sent_at = time.monotonic()
            while not self.stopped.is_set() and not active_response.complete:
                phase = "upstream-read"
                if not resume_validated:
                    remaining = resume_header_deadline - time.monotonic()
                    if remaining <= 0:
                        raise ResponseError("resume-header-timeout")
                    upstream.settimeout(min(1, remaining))
                before = time.monotonic()
                failure = None
                data = None
                try:
                    data = upstream.recv(64 * 1024)
                except TimeoutError:
                    if resume_policy and resume_validated:
                        failure = "upstream-poll"
                    elif resume_header_deadline is not None:
                        if time.monotonic() >= resume_header_deadline:
                            raise ResponseError("resume-header-timeout")
                        continue
                    else:
                        raise
                except OSError:
                    if resume_policy and resume_validated:
                        failure = "upstream-read-error"
                    else:
                        raise
                finally:
                    receive_wait = time.monotonic() - before
                    upstream_wait_ms += receive_wait * 1000
                if not resume_validated and time.monotonic() >= resume_header_deadline:
                    raise ResponseError("resume-header-timeout")
                if data == b"":
                    try:
                        active_response.eof()
                    except ResponseError:
                        if resume_policy and resume_validated:
                            failure = "upstream-truncated"
                        else:
                            raise
                    if active_response.complete:
                        reason = "upstream-eof"
                        break
                if data:
                    received += len(data)
                    if first_byte_ms is None:
                        first_byte_ms = round((time.monotonic() - sent_at) * 1000, 3)
                        self.diagnostic(sequence, "first-byte", ttfb_ms=first_byte_ms)
                    outgoing_data = active_response.feed(data)
                    status = response.status
                    if resume_attempts:
                        # Never expose a retry's headers or bytes before checking
                        # its identity and exact range, even in the same recv().
                        if active_response.mode is None:
                            continue
                        if not resume_validated:
                            resume_policy.validate(active_response, body_forwarded)
                            resume_validated = True
                            slow_transfer = SlowTransfer()
                            receive_wait = 0
                            upstream.settimeout(min(1, SLOW_WINDOW_SECONDS))
                            self.diagnostic(sequence, "resume-validated",
                                            attempt=resume_attempts, offset=body_forwarded)
                        outgoing_data = active_response.last_body
                    elif resume_policy is None and response.mode is not None:
                        resume_policy = ResumePolicy.for_response(response, outgoing)
                        if resume_policy:
                            slow_transfer = SlowTransfer()
                            receive_wait = 0
                            upstream.settimeout(min(1, SLOW_WINDOW_SECONDS))
                            self.diagnostic(sequence, "resumable", content_length=response.content_length)
                    if outgoing_data:
                        phase = "client-write"
                        before = time.monotonic()
                        response_started = True
                        try:
                            client.sendall(outgoing_data)
                        finally:
                            client_write_ms += (time.monotonic() - before) * 1000
                        forwarded += len(outgoing_data)
                        body_forwarded += len(active_response.last_body)
                if active_response.complete:
                    reason = "framing-complete"
                    break
                if slow_transfer and resume_validated:
                    slow_transfer.observe(receive_wait, len(active_response.last_body) if data else 0)
                    if failure == "upstream-poll" and not slow_transfer.slow():
                        continue
                    if failure == "upstream-poll" or slow_transfer.slow():
                        failure = "upstream-low-speed"
                if failure:
                    if self.stopped.is_set():
                        break
                    if not resume_policy or not 0 <= body_forwarded < resume_policy.length:
                        raise ResponseError(failure)
                    if resume_attempts >= RESUME_MAX_ATTEMPTS:
                        raise ResponseError("resume-limit")
                    resume_attempts += 1
                    self.diagnostic(sequence, "resume", attempt=resume_attempts,
                                    offset=body_forwarded, reason=failure,
                                    upstream_wait_ms=round(upstream_wait_ms, 3),
                                    client_write_ms=round(client_write_ms, 3))
                    with self.lock:
                        self.sockets.discard(upstream)
                    upstream.close()
                    upstream = None
                    phase = "resume-connect"
                    upstream = self.connector(self.interface, address, port)
                    with self.lock:
                        self.sockets.add(upstream)
                    if self.stopped.is_set():
                        break
                    # Header acquisition is bounded too; an unvalidated retry
                    # must never keep the original downstream waiting 90 s.
                    upstream.settimeout(SLOW_WINDOW_SECONDS)
                    phase = "resume-write"
                    upstream.sendall(resume_policy.request(outgoing, body_forwarded))
                    active_response = HTTPResponse()
                    resume_validated = False
                    resume_header_deadline = time.monotonic() + SLOW_WINDOW_SECONDS
                    slow_transfer = None
        except RequestError as error:
            status = error.status
            reason = "request-rejected"
            send_error(client, error.status, error.reason)
        except ResponseError as error:
            reason = str(error)
            if not response_started and not self.stopped.is_set():
                status = 502
                send_error(client, 502, "Invalid or incomplete upstream HTTP response")
        except (OSError, ValueError) as error:
            reason = phase + ("-timeout" if isinstance(error, TimeoutError) else "-error")
            error_number = getattr(error, "errno", None)
            # Never append a synthetic status to a partly forwarded response,
            # and never print exception text containing a request URL or token.
            if not response_started and not self.stopped.is_set():
                status = 502
                send_error(client, 502, "Direct HTTP upstream unavailable")
        finally:
            self.diagnostic(
                sequence, "closed", connect_ms=connect_ms, ttfb_ms=first_byte_ms,
                total_ms=round((time.monotonic() - started) * 1000, 3),
                upstream_wait_ms=round(upstream_wait_ms, 3),
                client_write_ms=round(client_write_ms, 3),
                upstream_bytes=received, forwarded_bytes=forwarded,
                status=status, framing=response.mode if response else None,
                content_length=response.content_length if response else None,
                body_bytes=body_forwarded,
                body_complete=(body_forwarded == resume_policy.length if resume_policy
                               else response.complete if response else False),
                resume_attempts=resume_attempts,
                reason="server-stop" if self.stopped.is_set() else reason,
                errno=error_number,
            )
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
            self.sequence += 1
            sequence = self.sequence
            if not self.slots.acquire(blocking=False):
                self.diagnostic(sequence, "closed", status=503, reason="connection-limit")
                send_error(client, 503, "Direct HTTP connection limit reached")
                client.close()
                continue
            worker = threading.Thread(target=self.handle, args=(client, sequence), daemon=True)
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
    parser.add_argument("--diagnostics", action="store_true",
                        help="bounded timing/framing diagnostics; never URLs, headers or tokens")
    args = parser.parse_args()
    if args.parent_pid is not None and args.parent_pid != os.getppid():
        parser.error("--parent-pid must identify this helper's launcher")
    with bound_socket(args.interface):
        pass
    server = DirectHTTPServer(args.interface, diagnostics=args.diagnostics)
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
