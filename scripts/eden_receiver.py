"""AirPlay identity read from the live NRO; shared by host discovery backends."""

import plistlib
import re
import socket

FEATURE_AUDIO = 1 << 9
FEATURE_RSA = 1 << 23
MAX_REPLY = 65536


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

