#!/usr/bin/env python3
"""Network selection, SSDP interoperability and live receiver lifetime tests."""
import http.client
import importlib.util
from pathlib import Path
import socket
import sys
import threading
from types import SimpleNamespace as Item
import unittest
from unittest.mock import Mock, patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "eden_lan", Path(__file__).with_name("eden-lan-discovery.py"))
lan = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = lan
spec.loader.exec_module(lan)

XML = b'''<root xmlns="urn:schemas-upnp-org:device-1-0">
<URLBase>http://198.18.0.1:49152/</URLBase><device>
<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>
<UDN>uuid:2766132e-0246-4b30-8558-c159a3698bdb</UDN>
<friendlyName>Switch TV Box</friendlyName><iconList><icon>
<url>http://198.18.0.1:49152/dlna/icon.jpg</url></icon></iconList>
<serviceList><service>
<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
<controlURL>AVTransport/action</controlURL><eventSubURL>AVTransport/event</eventSubURL>
<SCPDURL>dlna/AVTransport.xml</SCPDURL></service></serviceList>
<presentationURL>http://198.18.0.1:49152/presentation.html</presentationURL>
</device></root>'''


def entry(address, mask="255.255.255.0"):
    return Item(family=socket.AF_INET, address=address, netmask=mask)


class AutoDiscoveryTests(unittest.TestCase):
    def test_automatic_physical_selection_and_same_subnet_deduplication(self):
        addresses = {"Wi-Fi": [entry("192.168.1.10")], "Ethernet": [entry("192.168.1.20")],
                     "Other LAN": [entry("10.0.0.9")], "VPN": [entry("198.18.0.1")],
                     "Disconnected": [entry("10.1.0.1")], "Loopback": [entry("127.0.0.1")]}
        stats = {name: Item(isup=name != "Disconnected") for name in addresses}
        result = lan.select_adapters(addresses, stats, {"Wi-Fi", "Ethernet", "Other LAN", "Disconnected"})
        self.assertEqual({item.address for item in result}, {"192.168.1.20", "10.0.0.9"})
        addresses["Ethernet"] = [entry("192.168.1.21")]
        self.assertIn("192.168.1.21", {item.address for item in lan.select_adapters(addresses, stats, {"Ethernet"})})

    def test_no_physical_adapter_never_falls_back_to_vpn(self):
        self.assertEqual(lan.select_adapters({"Meta": [entry("198.18.0.1")]},
                                           {"Meta": Item(isup=True)}, set()), ())
        for address in ("0.0.0.0", "127.0.0.1", "169.254.1.2", "239.255.255.250", "255.255.255.255"):
            self.assertFalse(lan.usable_address(address))

    def test_wiliwili_search_packet_is_accepted(self):
        packet = ("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                  'MAN: "ssdp:discover"\r\nST: ' + lan.RENDERER + "\r\nMX: 1\r\n\r\n").encode()
        self.assertEqual(lan.search_target(packet), (lan.RENDERER, 1))
        self.assertEqual(lan.search_target(packet.replace(b"MX: 1", b"mx: 5")), (lan.RENDERER, 2))
        for invalid in (packet[:-2], packet.replace(b"MX: 1", b"MX: 0"),
                        packet.replace(b"MX: 1", b"MX: 900"),
                        packet.replace(b"MX: 1", b"MX: 1\r\nMX: 2"),
                        packet.replace(b"M-SEARCH", b"NOTIFY"), b"x" * 4097):
            self.assertIsNone(lan.search_target(invalid))

    def test_dlna_records_and_urls_follow_reachable_interface(self):
        root, records = lan.parse_description(XML)
        self.assertIn((lan.RENDERER, "uuid:2766132e-0246-4b30-8558-c159a3698bdb::" + lan.RENDERER), records)
        data = lan.description_for_interface(root, "192.168.1.20")
        self.assertNotIn(b"198.18.0.1", data)
        self.assertIn(b"http://192.168.1.20:49152/AVTransport/action", data)
        self.assertIn(b"http://192.168.1.20:49152/AVTransport/event", data)
        self.assertIn(b"198.18.0.1", ET.tostring(root))  # No mutation across adapters.
        for kind, usn in records:
            reply = lan.ssdp_packet("192.168.1.20", kind, usn)
            self.assertTrue(reply.startswith(b"HTTP/1.1 200 OK\r\n"))
            self.assertIn(b"LOCATION: http://192.168.1.20:18958/description.xml\r\n", reply)
            self.assertIn(("ST: " + kind + "\r\n").encode(), reply)
            self.assertNotIn(b"LOCATION:", lan.ssdp_packet("192.168.1.20", kind, usn, "byebye"))

    def test_invalid_or_unrelated_renderer_is_never_announced(self):
        for data in (b"", b"x" * 65537, b"<!DOCTYPE root>" + XML,
                     XML.replace(b"Switch TV Box", b"Other Player"),
                     XML.replace(b"MediaRenderer:1", b"MediaServer:1"),
                     XML.replace(b"uuid:2766132e-0246-4b30-8558-c159a3698bdb", b"uuid:bad"),
                     XML.replace(b"AVTransport:1", b"Unknown:1")):
            with self.subTest(data=data[:50]), self.assertRaises(ValueError):
                lan.parse_description(data)

    def test_pid_reuse_and_listener_loss_invalidate_identity(self):
        process = Mock()
        process.create_time.return_value = 100
        with patch.object(lan.psutil, "Process", return_value=process), \
             patch.object(lan, "listener_ports", return_value=frozenset({7000, 49152})):
            self.assertTrue(lan.still_owned((42, 100, frozenset({7000, 49152}))))
            self.assertFalse(lan.still_owned((42, 99, frozenset({7000, 49152}))))
            self.assertFalse(lan.still_owned((42, 100, frozenset({7000}))))

    def test_real_process_socket_inspection(self):
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            server.listen()
            port = server.getsockname()[1]
            self.assertTrue(any(item.laddr and item.laddr.port == port
                                for item in lan.connections(lan.psutil.Process())))

    def test_withdraw_closes_publishers_and_description_server(self):
        manager = lan.DiscoveryManager()
        publisher, http = Mock(), Mock()
        manager.publishers, manager.http = [publisher], http
        manager.identity = (42, 100, frozenset({7000}))
        manager.addresses = {"192.168.1.20"}
        manager.withdraw()
        publisher.close.assert_called_once()
        http.shutdown.assert_called_once()
        http.server_close.assert_called_once()
        self.assertIsNone(manager.identity)
        self.assertEqual(manager.addresses, set())

    def test_no_receiver_never_creates_a_fake_device(self):
        manager = lan.DiscoveryManager()
        with patch.object(lan, "find_receiver", return_value=None), \
             patch.object(lan, "physical_names", return_value=set()), \
             patch.object(lan, "MdnsPublisher") as mdns, patch.object(lan, "SsdpPublisher") as ssdp:
            manager.refresh()
            mdns.assert_not_called()
            ssdp.assert_not_called()

    def test_description_server_does_not_forward_control_and_withdraws_on_exit(self):
        manager = lan.DiscoveryManager()
        manager.identity = (42, 100, frozenset({49152}))
        manager.addresses = {"127.0.0.1"}
        manager.description = lan.parse_description(XML)[0]
        server = lan.ThreadingHTTPServer(("127.0.0.1", 0), lan.DescriptionHandler)
        server.manager = manager
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with patch.object(lan, "still_owned", return_value=True):
                client = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                client.request("GET", "/description.xml")
                result = client.getresponse()
                self.assertEqual(result.status, 200)
                self.assertIn(b"http://127.0.0.1:49152/AVTransport/action", result.read())
                client.close()
            with patch.object(lan, "still_owned", return_value=False):
                client = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                client.request("GET", "/description.xml")
                self.assertEqual(client.getresponse().status, 503)
                client.close()
            client = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            client.request("GET", "/AVTransport/action")
            self.assertEqual(client.getresponse().status, 404)
            client.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
