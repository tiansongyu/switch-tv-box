#!/usr/bin/env python3
"""Exercise the composed AirPlay receiver without exposing pairing secrets."""

from __future__ import annotations

import plistlib
import base64
import socket
import subprocess
from pathlib import Path

from smoke_airplay import ResponseReader, connect, launch_server, wait_for_ready


def request(connection: socket.socket, cseq: int, method: str, uri: str,
            body: bytes = b"", content_type: str | None = None,
            extra_headers: dict[str, str] | None = None) -> None:
    headers = [
        f"{method} {uri} RTSP/1.0",
        f"CSeq: {cseq}",
        f"Content-Length: {len(body)}",
    ]
    if content_type:
        headers.append(f"Content-Type: {content_type}")
    for name, value in (extra_headers or {}).items():
        headers.append(f"{name}: {value}")
    connection.sendall(("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body)

def photo_request(connection: socket.socket, session_id: str, action: str,
                  body: bytes = b"") -> None:
    headers = [
        "PUT /photo HTTP/1.1",
        f"X-Apple-Session-ID: {session_id}",
        "X-Apple-AssetKey: family-photo",
        f"X-Apple-AssetAction: {action}",
        "Content-Type: image/png",
        f"Content-Length: {len(body)}",
    ]
    connection.sendall(("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body)


def finish_connection(connection: socket.socket) -> None:
    # EOF is sent after the receiver's session_closed callback, providing a
    # deterministic barrier without sleeps before the next control connection.
    connection.shutdown(socket.SHUT_WR)
    assert connection.recv(1) == b""


def run_smoke(server_binary: Path, requested_port: int = 0) -> None:
    process = launch_server(server_binary, requested_port)
    try:
        port = wait_for_ready(process)

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            request(connection, 1, "GET", "/info")
            protocol, status, headers, body = reader.read()
            assert protocol == "RTSP/1.0" and status == 200
            assert headers.get("cseq") == "1"
            assert headers.get("content-type") == "application/x-apple-binary-plist"
            assert body.startswith(b"bplist00")
            info = plistlib.loads(body)
            assert info["features"] == (
                (1 << 27) | (1 << 23) | (1 << 13) | (1 << 9) |
                (1 << 8) | (1 << 7) | (1 << 4) | (1 << 1) | (1 << 0)
            )

            stage1 = b"FPLY" + bytes((3,)) + bytes(11)
            request(connection, 2, "POST", "/fp-setup", stage1,
                    "application/octet-stream")
            _protocol, status, headers, body = reader.read()
            assert status == 200 and headers.get("cseq") == "2"
            assert headers.get("content-type") == "application/octet-stream"
            assert len(body) == 142 and body.startswith(b"FPLY")

            request(connection, 3, "SETUP", "/stream")
            _protocol, status, headers, body = reader.read()
            assert status == 470 and headers.get("connection") == "close" and body == b""
            assert connection.recv(1) == b""

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            request(connection, 4, "OPTIONS", "*")
            _protocol, status, headers, body = reader.read()
            assert status == 200 and "SETUP" in headers.get("public", "") and body == b""

        # The complete old RAOP path is deliberately exercised without pairing.
        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            request(connection, 1, "OPTIONS", "*", extra_headers={
                "Apple-Challenge": "AAECAwQFBgcICQoLDA0ODw"})
            _protocol, status, headers, _body = reader.read()
            assert status == 200 and "ANNOUNCE" in headers["public"]
            response = headers["apple-response"]
            assert len(base64.b64decode(response + "==")) == 256
            sdp = (
                b"v=0\r\no=iTunes 42 0 IN IP4 127.0.0.1\r\ns=AirTunes\r\n"
                b"c=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 0 RTP/AVP 96\r\n"
                b"a=rtpmap:96 AppleLossless\r\n"
                b"a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n")
            request(connection, 2, "ANNOUNCE", "rtsp://127.0.0.1/42",
                    sdp, "application/sdp")
            assert reader.read()[1] == 200
            request(connection, 3, "SETUP", "rtsp://127.0.0.1/42",
                    extra_headers={"Transport": "RTP/AVP/UDP;unicast;mode=record;"
                                   "control_port=6001;timing_port=6002"})
            _protocol, status, headers, _body = reader.read()
            assert status == 200
            assert "server_port=7012" in headers["transport"]
            assert "control_port=7013" in headers["transport"]
            assert "timing_port=7010" in headers["transport"]
            assert headers.get("session")
            request(connection, 4, "RECORD", "rtsp://127.0.0.1/42")
            assert reader.read()[1] == 200
            request(connection, 5, "SET_PARAMETER", "rtsp://127.0.0.1/42",
                    b"volume: -22.5\r\n", "text/parameters")
            assert reader.read()[1] == 200
            request(connection, 6, "GET_PARAMETER", "rtsp://127.0.0.1/42",
                    b"volume\r\n", "text/parameters")
            assert reader.read()[3] == b"volume: -22.500000\r\n"
            request(connection, 7, "FLUSH", "rtsp://127.0.0.1/42",
                    extra_headers={"RTP-Info": "seq=65535;rtptime=4294967295"})
            assert reader.read()[1] == 200
            request(connection, 8, "TEARDOWN", "rtsp://127.0.0.1/42")
            assert reader.read()[1] == 200

        # HTTP AirPlay 1 cache operations need no pairing, but stay session scoped.
        with connect("127.0.0.1", port) as first, \
                connect("127.0.0.1", port) as second, \
                connect("127.0.0.1", port) as stranger:
            first_reader = ResponseReader(first)
            second_reader = ResponseReader(second)
            stranger_reader = ResponseReader(stranger)
            png = base64.b64decode(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8"
                "/x8AAwMCAO+a2ioAAAAASUVORK5CYII=")
            photo_request(first, "photo-session", "cacheOnly", png)
            assert first_reader.read()[1] == 200
            photo_request(stranger, "other-photo-session", "displayCached")
            assert stranger_reader.read()[1] == 412
            photo_request(second, "photo-session", "displayCached")
            assert second_reader.read()[1] == 200
            first.close()
            # A second connection with the same session retains the photo.
            photo_request(second, "photo-session", "displayCached")
            assert second_reader.read()[1] == 200
            second.sendall(
                b"POST /stop HTTP/1.1\r\nX-Apple-Session-ID: photo-session\r\n"
                b"Content-Length: 0\r\n\r\n")
            assert second_reader.read()[1] == 200
            photo_request(second, "photo-session", "displayCached")
            assert second_reader.read()[1] == 412

        # Cache ownership outlives the last HTTP socket. Thirty-two independent
        # short sessions also exceed the manager's capacity unless eviction
        # releases old cache-only logical identities.
        for index in range(32):
            with connect("127.0.0.1", port) as connection:
                reader = ResponseReader(connection)
                photo_request(connection, f"short-photo-{index}", "cacheOnly", png)
                assert reader.read()[1] == 200
                finish_connection(connection)

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            photo_request(connection, "short-photo-0", "displayCached")
            assert reader.read()[1] == 412  # evicted, never another client's image
            finish_connection(connection)

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            photo_request(connection, "short-photo-31", "displayCached")
            assert reader.read()[1] == 200
            finish_connection(connection)

        # A displayed image also survives control detachment; explicit stop
        # clears both the player and its session's cached assets.
        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            photo_request(connection, "short-photo-31", "displayCached")
            assert reader.read()[1] == 200
            connection.sendall(
                b"POST /stop HTTP/1.1\r\nX-Apple-Session-ID: short-photo-31\r\n"
                b"Content-Length: 0\r\n\r\n")
            assert reader.read()[1] == 200
            finish_connection(connection)

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            photo_request(connection, "short-photo-31", "displayCached")
            assert reader.read()[1] == 412
            finish_connection(connection)

        process.terminate()
        process.wait(timeout=3.0)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)
        if process.returncode != 0:
            stderr = process.stderr.read() if process.stderr else ""
            raise AssertionError(f"receiver smoke failed: {process.returncode}: {stderr}")
