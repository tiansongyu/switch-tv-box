#!/usr/bin/env python3
"""Offline tests for the optional relay and launcher-owned lifetime."""

import argparse
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


relay = module("iptv_direct", ROOT / "scripts/eden-iptv-direct.py")
launcher = module("eden_launcher", ROOT / "scripts/run-eden.py")


def receive_all(sock):
    reply = bytearray()
    while True:
        try:
            data = sock.recv(65536)
        except ConnectionResetError:
            # Rejecting an oversized request can close with unread client data.
            # The bounded error response must still have arrived in full.
            header, separator, body = bytes(reply).partition(b"\r\n\r\n")
            lengths = [line.split(b":", 1)[1].strip()
                       for line in header.split(b"\r\n")
                       if line.lower().startswith(b"content-length:")]
            if separator and len(lengths) == 1 and int(lengths[0]) == len(body):
                return bytes(reply)
            raise
        if not data:
            return bytes(reply)
        reply.extend(data)


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.connector = mock.Mock(side_effect=AssertionError("Unexpected upstream connection"))
        self.server = relay.DirectHTTPServer("fixture0", connector=self.connector, port=0)
        self.thread = threading.Thread(target=self.server.serve, daemon=True)
        self.thread.start()
        self.addCleanup(self.close)

    def close(self):
        self.server.close()
        self.thread.join(timeout=2)
        self.assertFalse(self.thread.is_alive())

    def client(self):
        return socket.create_connection(("127.0.0.1", self.server.port), timeout=3)

    def request(self, request):
        with self.client() as client:
            client.sendall(request)
            return receive_all(client)

    def fixture_upstream(self, response):
        accepted = []
        peer = socket.socket()
        peer.bind(("127.0.0.1", 0))
        peer.listen(1)
        peer.settimeout(3)
        self.addCleanup(peer.close)

        def serve():
            with peer.accept()[0] as client:
                client.settimeout(3)
                headers = b""
                while b"\r\n\r\n" not in headers:
                    headers += client.recv(4096)
                accepted.append(headers)
                # Deliberate fragmented writes exercise stream forwarding.
                for start in range(0, len(response), 1379):
                    client.sendall(response[start:start + 1379])

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        self.addCleanup(worker.join, 3)
        self.connector.side_effect = lambda *_: socket.create_connection(peer.getsockname(), 3)
        return accepted, worker

    def test_range_binary_body_status_and_query_are_preserved(self):
        payload = bytes(range(256)) * 1027
        response = (b"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 9-262920/900000\r\n"
                    + f"Content-Length: {len(payload)}\r\n".encode()
                    + b"Connection: close\r\n\r\n" + payload)
        accepted, worker = self.fixture_upstream(response)
        reply = self.request(
            b"GET http://74.91.26.218:82/live/cctv3hd.m3u8?tm=1&key=a%2Fb HTTP/1.1\r\n"
            b"Host: wrong.example\r\nRange: bytes=9-262920\r\nUser-Agent: fixture\r\n"
            b"Proxy-Authorization: secret\r\nConnection: keep-alive, X-Hop\r\n"
            b"X-Hop: do-not-forward\r\n\r\n"
        )
        worker.join(3)
        self.assertEqual(reply, response)
        self.connector.assert_called_once_with("fixture0", "74.91.26.218", 82)
        self.assertEqual(accepted, [
            b"GET /live/cctv3hd.m3u8?tm=1&key=a%2Fb HTTP/1.1\r\n"
            b"Host: 74.91.26.218:82\r\nConnection: close\r\n"
            b"Range: bytes=9-262920\r\nUser-Agent: fixture\r\n\r\n"
        ])

    def test_redirect_token_is_not_rewritten_or_logged(self):
        response = (b"HTTP/1.1 302 Found\r\n"
                    b"Location: http://63.141.239.226:81/live/file.m3u8?key=private-token\r\n"
                    b"Content-Length: 0\r\nConnection: close\r\n\r\n")
        self.fixture_upstream(response)
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            reply = self.request(b"GET http://74.91.26.218/a HTTP/1.1\r\n\r\n")
        self.assertEqual(reply, response)
        self.assertEqual(output.getvalue(), "")

    def test_head_and_chunked_response_are_unchanged(self):
        for method, response in (
            (b"HEAD", b"HTTP/1.1 200 OK\r\nContent-Length: 99999\r\nConnection: close\r\n\r\n"),
            (b"GET", b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                     b"3\r\nabc\r\n2\r\n\x00\xff\r\n0\r\n\r\n"),
        ):
            with self.subTest(method=method):
                accepted, worker = self.fixture_upstream(response)
                expected = response
                if b"Connection: close\r\n" not in expected:
                    expected = expected.replace(b"\r\n\r\n", b"\r\nConnection: close\r\n\r\n", 1)
                self.assertEqual(self.request(method + b" http://74.91.26.218/a? HTTP/1.1\r\n\r\n"),
                                 expected)
                worker.join(3)
                self.assertTrue(accepted[0].startswith(method + b" /a? HTTP/1.1\r\n"))

    def test_unsupported_or_private_target_never_connects(self):
        cases = [
            (b"https://74.91.26.218/a", 501), (b"http://example.com/a", 501),
            (b"http://[::1]/a", 501), (b"/relative", 501),
            (b"http://u:p@74.91.26.218/a", 400), (b"http://74.91.26.218/a#", 400),
            (b"http://074.91.26.218/a", 400), (b"http://74.91.26.218:0/a", 400),
            (b"http://74.91.26.218:65536/a", 400),
        ]
        for address in ("0.1.2.3", "10.30.0.44", "100.64.1.1", "127.0.0.1",
                        "169.254.1.1", "172.16.0.1", "192.0.0.9", "192.0.2.3",
                        "192.88.99.1", "192.168.1.1", "198.18.0.1",
                        "198.51.100.1", "203.0.113.1", "224.0.0.1", "255.255.255.255"):
            cases.append((f"http://{address}/a".encode(), 403))
        for target, status in cases:
            with self.subTest(target=target):
                reply = self.request(b"GET " + target + b" HTTP/1.1\r\n\r\n")
                self.assertTrue(reply.startswith(f"HTTP/1.1 {status} ".encode()), reply)
        self.connector.assert_not_called()

    def test_invalid_headers_methods_and_body_never_connect(self):
        for request, status in (
            (b"CONNECT 74.91.26.218:443 HTTP/1.1\r\n\r\n", 405),
            (b"POST http://74.91.26.218/a HTTP/1.1\r\n\r\n", 405),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\nContent-Length: 1\r\n\r\nx", 400),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 400),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\nX: good\nInjected: yes\r\n\r\n", 400),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\n folded: bad\r\n\r\n", 400),
            (b"GET http://74.91.26.218/a HTTP/1.1\r\nX: " + b"a" * relay.HEADER_LIMIT, 431),
        ):
            with self.subTest(status=status, prefix=request[:30]):
                self.assertTrue(self.request(request).startswith(f"HTTP/1.1 {status} ".encode()))
        self.connector.assert_not_called()

    def test_upstream_errors_have_no_url_or_token(self):
        self.connector.side_effect = OSError("token=never-log-me")
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            reply = self.request(b"GET http://74.91.26.218/a?key=never-log-me HTTP/1.1\r\n\r\n")
        self.assertTrue(reply.startswith(b"HTTP/1.1 502 "))
        self.assertNotIn(b"never-log-me", reply)
        self.assertEqual(output.getvalue(), "")

    def test_completed_responses_close_without_waiting_for_upstream_eof(self):
        for body, headers in (
            (b"abc\x00\xff", b"Content-Length: 5"),
            (b"3;ignored=yes\r\nabc\r\n2\r\n\x00\xff\r\n0\r\nX-Trailer: fine\r\n\r\n",
             b"Transfer-Encoding: chunked"),
            (b"", b"Content-Length: 0"),
        ):
            with self.subTest(headers=headers):
                relay_side, origin = socket.socketpair()
                release = threading.Event()
                observed_close = threading.Event()
                self.connector.side_effect = lambda *_: relay_side

                def serve():
                    with origin:
                        incoming = b""
                        while b"\r\n\r\n" not in incoming:
                            incoming += origin.recv(4096)
                        response = (b"HTTP/1.1 200 OK\r\n" + headers
                                    + b"\r\nConnection: keep-alive\r\n\r\n" + body)
                        # Split framing markers across recv boundaries.
                        for byte in response:
                            origin.sendall(bytes([byte]))
                        origin.settimeout(1)
                        if origin.recv(1) == b"":
                            observed_close.set()
                        release.wait(2)

                worker = threading.Thread(target=serve, daemon=True)
                worker.start()
                try:
                    start = time.monotonic()
                    reply = self.request(b"GET http://74.91.26.218/a HTTP/1.1\r\n\r\n")
                    self.assertLess(time.monotonic() - start, 1)
                    header, payload = reply.split(b"\r\n\r\n", 1)
                    self.assertIn(b"Connection: close", header)
                    self.assertNotIn(b"keep-alive", header)
                    self.assertEqual(payload, body)
                    self.assertTrue(observed_close.wait(1))
                finally:
                    release.set()
                    worker.join(3)

    def test_diagnostics_report_complete_truncated_and_timeout_without_secrets(self):
        records = []
        self.server.diagnostics = True
        self.server.diagnostic_sink = records.append
        for body, length, reason, complete in (
            (b"abc", 3, "framing-complete", True),
            (b"abc", 99, "upstream-truncated", False),
        ):
            with self.subTest(reason=reason):
                self.fixture_upstream(
                    f"HTTP/1.1 206 Partial Content\r\nContent-Length: {length}\r\n".encode()
                    + b"Location: http://secret.example/path?key=do-not-log\r\n\r\n" + body)
                reply = self.request(b"GET http://74.91.26.218/path?key=do-not-log HTTP/1.1\r\n\r\n")
                self.assertTrue(reply.endswith(body))
                closed = [entry for entry in records if entry["event"] == "closed"][-1]
                self.assertEqual(closed["reason"], reason)
                self.assertEqual(closed["body_complete"], complete)
                self.assertEqual(closed["status"], 206)
                self.assertEqual(closed["body_bytes"], 3)
                self.assertIsNotNone(closed["connect_ms"])
                self.assertIsNotNone(closed["ttfb_ms"])
                self.assertGreaterEqual(closed["total_ms"], 0)
        self.connector.side_effect = TimeoutError("private-token")
        self.request(b"GET http://74.91.26.218/path?key=private-token HTTP/1.1\r\n\r\n")
        closed = [entry for entry in records if entry["event"] == "closed"][-1]
        self.assertEqual(closed["reason"], "upstream-connect-timeout")
        self.assertEqual(closed["status"], 502)
        serialized = str(records)
        for secret in ("74.91.26.218", "secret.example", "do-not-log", "private-token", "/path"):
            self.assertNotIn(secret, serialized)

    def test_chunk_framing_all_split_points_and_truncation(self):
        header = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        body = b"1;extension=value\r\na\r\n4\r\nbcde\r\n0\r\nX-Safe: yes\r\n\r\n"
        original = header + body
        expected = header.replace(b"\r\n\r\n", b"\r\nConnection: close\r\n\r\n") + body
        for split in range(len(original) + 1):
            response = relay.HTTPResponse()
            self.assertEqual(response.feed(original[:split]) + response.feed(original[split:]),
                             expected)
            self.assertTrue(response.complete)
        response = relay.HTTPResponse()
        response.feed(original[:-1])
        self.assertFalse(response.complete)
        with self.assertRaisesRegex(relay.ResponseError, "upstream-truncated"):
            response.eof()

    def test_diagnostics_are_bounded_and_defaults_are_silent(self):
        records = []
        self.server.diagnostic_sink = records.append
        self.server.diagnostic(1, "fixture")
        self.assertEqual(records, [])
        self.server.diagnostics = True
        self.server.diagnostic(relay.DIAGNOSTIC_REQUEST_LIMIT, "fixture")
        self.server.diagnostic(relay.DIAGNOSTIC_REQUEST_LIMIT + 1, "fixture")
        self.assertEqual(len(records), 1)

    def test_eight_clients_and_close_interrupt_idle_receives(self):
        clients = [self.client() for _ in range(relay.MAX_CLIENTS)]
        for client in clients:
            self.addCleanup(client.close)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            with self.server.lock:
                if len(self.server.workers) == relay.MAX_CLIENTS:
                    break
            time.sleep(0.01)
        with self.server.lock:
            self.assertEqual(len(self.server.workers), relay.MAX_CLIENTS)
        with self.client() as ninth:
            self.assertTrue(receive_all(ninth).startswith(b"HTTP/1.1 503 "))
        start = time.monotonic()
        self.server.close()
        self.assertLess(time.monotonic() - start, 2)
        with self.server.lock:
            self.assertFalse(self.server.workers)
            self.assertFalse(self.server.sockets)

    def test_connector_binds_before_connect_and_does_not_resolve(self):
        sock = mock.Mock()
        with mock.patch.object(relay.socket, "socket", return_value=sock), \
             mock.patch.object(relay.socket, "if_nametoindex", return_value=2), \
             mock.patch.object(relay.socket, "getaddrinfo", side_effect=AssertionError("DNS")):
            self.assertIs(relay.connect_bound("enp7s0", "74.91.26.218", 82), sock)
        self.assertEqual(sock.method_calls, [
            mock.call.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, b"enp7s0\0"),
            mock.call.settimeout(5), mock.call.connect(("74.91.26.218", 82)),
            mock.call.settimeout(90),
        ])


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="tvbox-direct-launcher-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.state = self.root / "state"
        self.state.mkdir()
        self.marker = self.state / "data/eden/sdmc/switch/switch-tv-box" / launcher.IPTV_DIRECT_MARKER
        self.fake = self.root / "helper.py"
        self.fake.write_text(
            "import argparse,os,signal,time\n"
            "p=argparse.ArgumentParser();p.add_argument('--interface');p.add_argument('--parent-pid');"
            "p.add_argument('--ready-fd',type=int);p.add_argument('--diagnostics',action='store_true');"
            "a=p.parse_args()\n"
            "os.write(a.ready_fd,b'R');os.close(a.ready_fd)\n"
            "while True:time.sleep(0.1)\n"
        )
        self.patches = contextlib.ExitStack()
        self.addCleanup(self.patches.close)
        self.patches.enter_context(mock.patch.object(launcher, "ROOT", self.root))
        self.patches.enter_context(mock.patch.object(launcher, "IPTV_DIRECT_HELPER", self.fake))

    def test_marker_exists_only_after_ready_and_child_is_cleaned(self):
        with tempfile.TemporaryFile() as log:
            with launcher.iptv_direct_session(self.state, "fixture0", log) as child:
                self.assertIsNone(child.poll())
                self.assertTrue(self.marker.is_file())
                self.assertEqual(self.marker.stat().st_mode & 0o777, 0o600)
            self.assertIsNotNone(child.poll())
            self.assertFalse(self.marker.exists())

    def test_launcher_diagnostics_are_explicitly_forwarded(self):
        original = subprocess.Popen
        with tempfile.TemporaryFile() as log, \
             mock.patch.object(launcher.subprocess, "Popen", wraps=original) as popen:
            with launcher.iptv_direct_session(self.state, "fixture0", log, diagnostics=True):
                self.assertIn("--diagnostics", popen.call_args.args[0])
        self.assertFalse(self.marker.exists())

    def test_failed_readiness_leaves_no_marker_or_child(self):
        self.fake.write_text("raise SystemExit(4)\n")
        children = []
        original = subprocess.Popen

        def popen(*args, **kwargs):
            child = original(*args, **kwargs)
            children.append(child)
            return child

        with mock.patch.object(launcher.subprocess, "Popen", side_effect=popen), \
             tempfile.TemporaryFile() as log:
            with self.assertRaisesRegex(RuntimeError, "did not become ready"):
                with launcher.iptv_direct_session(self.state, "fixture0", log):
                    self.fail("not ready")
        self.assertFalse(self.marker.exists())
        self.assertTrue(all(child.poll() is not None for child in children))

    def test_exception_cleanup_preserves_a_replaced_marker(self):
        with tempfile.TemporaryFile() as log:
            with self.assertRaisesRegex(RuntimeError, "fixture"):
                with launcher.iptv_direct_session(self.state, "fixture0", log) as child:
                    replacement = self.marker.with_suffix(".replacement")
                    replacement.write_bytes(b"another owner")
                    os.replace(replacement, self.marker)
                    raise RuntimeError("fixture")
        self.assertIsNotNone(child.poll())
        self.assertEqual(self.marker.read_bytes(), b"another owner")

    def test_prepare_clears_stale_marker_without_starting_helper(self):
        package = self.root / "fixture.zip"
        nro = self.root / "fixture.nro"
        nro.write_bytes(b"fixture")
        with zipfile.ZipFile(package, "w") as archive:
            for name in ("switch-tv-box.nro", "fonts/switch_font.ttf",
                         "iptv/sources.txt", "licenses/LICENSE.NX-Cast.txt"):
                archive.writestr(f"switch/switch-tv-box/{name}", b"fixture")
        self.marker.parent.mkdir(parents=True)
        self.marker.write_bytes(b"stale")
        with mock.patch.object(launcher, "PACKAGE", package), \
             mock.patch.object(launcher, "CONTROL_TEMPLATE", self.root / "absent"), \
             mock.patch.object(launcher.subprocess, "Popen") as popen:
            launcher.prepare(self.state, nro, "fixture0")
        self.assertFalse(self.marker.exists())
        popen.assert_not_called()

    def test_check_is_short_lived_and_creates_no_runtime_marker(self):
        args = argparse.Namespace(interface="fixture0", address=None, prepare_only=True,
                                  iptv_direct_http=True)
        nro = self.root / "fixture.nro"
        nro.write_bytes(b"\0" * 16 + b"NRO0" + b"\0" * 108)
        archive = mock.MagicMock()
        archive.testzip.return_value = None
        with mock.patch.object(launcher, "require_stopped"), \
             mock.patch.object(launcher, "lan_address", return_value="10.30.0.44"), \
             mock.patch.object(launcher, "EDEN", Path("/usr/bin/python3")), \
             mock.patch.object(launcher.zipfile, "ZipFile") as opened, \
             mock.patch.object(launcher, "package_files", return_value=[]), \
             mock.patch.object(launcher.subprocess, "run") as run:
            opened.return_value.__enter__.return_value = archive
            self.assertEqual(launcher.preflight(args, self.state, nro), "10.30.0.44")
        self.assertEqual(run.call_count, 1)
        self.assertIn("--check", run.call_args.args[0])
        self.assertFalse(self.marker.exists())

    def test_launcher_modes_never_enable_relay_implicitly(self):
        nro = self.root / "fixture.nro"
        for arguments, launches, enabled, diagnostics in (
            (["--check", "--iptv-direct-http"], False, None, False),
            (["--prepare-only", "--iptv-direct-http"], False, None, False),
            ([], True, False, False),
            (["--iptv-direct-http"], True, True, False),
            (["--iptv-direct-http", "--iptv-direct-http-diagnostics"], True, True, True),
        ):
            argv = ["run-eden.py", "--interface", "fixture0",
                    "--state", str(self.state), "--nro", str(nro)] + arguments
            with self.subTest(arguments=arguments), \
                 mock.patch.object(launcher.sys, "argv", argv), \
                 mock.patch.object(launcher, "preflight", return_value="10.30.0.44"), \
                 mock.patch.object(launcher, "require_stopped"), \
                 mock.patch.object(launcher, "prepare", return_value=nro) as prepare, \
                 mock.patch.object(launcher, "launch", return_value=0) as launch, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(launcher.main(), 0)
                self.assertEqual(launch.call_count, int(launches))
                if launches:
                    self.assertIs(launch.call_args.args[-2], enabled)
                    self.assertIs(launch.call_args.args[-1], diagnostics)
                if "--check" in arguments:
                    prepare.assert_not_called()
            self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
