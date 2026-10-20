#!/usr/bin/env python3
"""Host checks for live receiver identity and discovery capability fidelity."""

import importlib.util
import os
from pathlib import Path
import plistlib
import socket
import subprocess
import sys
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "eden_discovery", Path(__file__).with_name("eden-airplay-advertise.py"))
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class DiscoveryTests(unittest.TestCase):
    def identity(self, features):
        info = {
            "deviceID": "02:00:00:00:00:01", "name": "Test TV",
            "features": features, "pk": bytes(range(32)), "pi": "test-pairing",
            "model": "AppleTV3,2", "sourceVersion": "220.68",
        }
        txt = {
            "deviceid": info["deviceID"], "features": f"0x{features:X},0x0",
            "pk": info["pk"].hex(), "pi": info["pi"], "pw": "true",
        }
        return info, txt

    def test_legacy_pin_and_metadata_match_receiver(self):
        info, txt = self.identity(helper.FEATURE_AUDIO | helper.FEATURE_RSA |
                                  (1 << 17) | (1 << 16))
        _, records = helper.service_records(info, txt)
        raop = records[1][2]
        self.assertEqual(raop["et"], "0,1,3,5")
        self.assertEqual(raop["pw"], "false")
        self.assertEqual(raop["sf"], "0x4")
        self.assertEqual(raop["md"], "0,2")
        self.assertIs(records[0][2], txt)

    def test_modern_pin_does_not_claim_rsa(self):
        _, records = helper.service_records(*self.identity(helper.FEATURE_AUDIO))
        raop = records[1][2]
        self.assertEqual(raop["et"], "0,3,5")
        self.assertEqual(raop["pw"], "true")
        self.assertEqual(raop["sf"], "0x8c")
        self.assertEqual(raop["md"], "")

    def test_no_audio_capability_no_raop(self):
        _, records = helper.service_records(*self.identity(1))
        self.assertEqual([record[1] for record in records], ["_airplay._tcp"])

    def test_mixed_identity_is_rejected(self):
        info, txt = self.identity(helper.FEATURE_AUDIO)
        txt["deviceid"] = "02:00:00:00:00:02"
        with self.assertRaises(ValueError):
            helper.service_records(info, txt)

    def test_malformed_txt_is_rejected(self):
        for data in (b"", b"\x09key=x", b"\x00", b"\x03a=b\x03a=c"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                helper.decode_txt(data)
        self.assertEqual(helper.decode_txt(b"\x03a=b"), {"a": "b"})

    def test_direct_probe_ignores_proxy_and_checks_owner(self):
        request = []
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            server.listen()
            port = server.getsockname()[1]
            self.assertTrue(helper.listening(os.getpid(), "127.0.0.1", port))
            child = subprocess.Popen([sys.executable, "-c",
                                      "import sys; sys.stdin.read()"],
                                     stdin=subprocess.PIPE)
            try:
                self.assertFalse(helper.listening(child.pid, "127.0.0.1", port))
            finally:
                child.communicate(timeout=2)

            def serve():
                with server.accept()[0] as connection:
                    connection.settimeout(2)
                    raw = b""
                    while b"\r\n\r\n" not in raw:
                        raw += connection.recv(4096)
                    headers, body = raw.split(b"\r\n\r\n", 1)
                    size = int(next(line.split(b":", 1)[1] for line in
                                    headers.split(b"\r\n")
                                    if line.startswith(b"Content-Length:")))
                    while len(body) < size:
                        body += connection.recv(4096)
                    request.append(plistlib.loads(body))
                    result = plistlib.dumps({"txtAirPlay": b"\x03a=b"},
                                             fmt=plistlib.FMT_BINARY)
                    connection.sendall(
                        f"RTSP/1.0 200 OK\r\nContent-Length: {len(result)}\r\n\r\n"
                        .encode() + result)

            worker = threading.Thread(target=serve)
            worker.start()
            with patch.dict(os.environ, {"ALL_PROXY": "http://127.0.0.1:1",
                                         "HTTP_PROXY": "http://127.0.0.1:1"}):
                result = helper.get_info("127.0.0.1", port, True)
            worker.join(timeout=3)
            self.assertFalse(worker.is_alive())
            self.assertEqual(request, [{"qualifier": ["txtAirPlay"]}])
            self.assertEqual(result["txtAirPlay"], b"\x03a=b")


if __name__ == "__main__":
    unittest.main()
