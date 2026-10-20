# AirPlay 1 implementation and validation

Switch TV Box extends NX-Cast 0.3.2 with classic AirPlay 1 audio and photos.
The inherited URL video, HLS, paired mirroring, DLNA and IPTV pipelines remain.
The full reference book is available [offline](reference/README.md).

## Implemented

| Path | Behavior |
|---|---|
| Discovery | `_airplay._tcp` and `_raop._tcp`, stable per-device identity, Switch TV Box name, capability-gated TXT records |
| Classic audio | RTSP OPTIONS, ANNOUNCE SDP, UDP SETUP, RECORD, GET/SET_PARAMETER, FLUSH, TEARDOWN |
| Authentication | AirPort-compatible RSA Apple-Challenge response; RSA-OAEP AES key unwrap; unencrypted RAOP |
| Formats | Negotiated ALAC cookie; AAC-LC/AAC-ELD; 16-bit big-endian PCM (mono/stereo, 44.1/48 kHz) |
| Transport | RTP headers/extensions/padding, sequence wrap, bounded reorder queue, retransmit requests and replies, timing requests/responses |
| Playback | Audio-only Matroska stream to the existing mpv/FFmpeg/hos-audio player; actual ownership and teardown |
| Controls | dB volume/mute, RTP timestamp/sequence FLUSH, generation replacement and queue reset |
| Photos | JPEG/PNG PUT `/photo`, `cacheOnly`, `displayCached`, 412 on cache miss; session-isolated bounded cache |
| Photo display | Loopback-only image delivery to mpv, indefinite still display until stop/replacement; active image protected from cache eviction |
| Video | Existing `/play`, `/rate`, `/scrub`, `/playback-info`, `/stop`, reverse HLS |
| Mirroring | Existing PIN pairing, FairPlay interoperability backend, binary-plist SETUP type 110 and accompanying type 96 audio |

Classic HTTP video/photos and ANNOUNCE-based RAOP do not require the modern
PIN pairing exchange. Paired mirroring retains its separate authentication.
Feature advertising excludes unsupported slideshow and metadata UI features.

Request bodies are limited to 4 MiB. Photos use four cache slots, at most 4 MiB
per image and 16 MiB total. Cache keys are opaque identifiers, never file paths.
Cache-only uploads do not take over playback. Normal HTTP disconnects retain an
active photo/video; explicit stop and ownership replacement retire its resources.
Precached photos also survive HTTP reconnects with the same Apple session ID.
Their logical sessions stay referenced only while a bounded cache slot exists;
eviction or explicit stop reclaims these references without taking playback.

Standalone music stays on the home screen, with a music card and pause/resume
control, rather than showing an empty video screen. The receiver keeps the
sender's RTP sample clock; timestamps are not compared to an absent video clock.

## Validation

Run the full host suite in the same container used to build the NRO:

```sh
./scripts/build-local.sh test
./scripts/build-local.sh build
```

Tests cover real TCP receiver routing, RSA responses verified with the public
key, encrypted and clear SDP negotiation, malformed requests, session isolation,
FLUSH, retransmission, teardown and photo cache lifetime. Photo tests include
32 independent short-lived TCP sessions, reconnecting after cache-only uploads,
eviction, and reentrant playback claims/stops from the same session. Audio tests encode
compressed ALAC with FFmpeg, encrypt it, send/decrypt RTP, mux Matroska and decode
back to samples. PCM is also compared after decoding. A timestamp test spans
120 seconds and a 32-bit RTP wrap. AddressSanitizer/UndefinedBehaviorSanitizer
checks supplement the host tests. These exercise the software pipeline; they
are not measurements from a physical Switch or Apple device.

The Switch build requires ALAC, AAC, PCM, JPEG and PNG decoders, image demuxers,
the Matroska muxer and libnx-backed randomness. The release checks reject a
toolchain missing these components. See [BUILD.md](BUILD.md).

## Remaining compatibility limits

- Physical iPhone/iPad/Mac → Switch interoperability, TV audio output, Wi-Fi
  loss/jitter, long playback and suspend/resume still require device testing.
- The old Apple TV `/stream` and `/stream.xml` mirroring transport is not
  implemented. The inherited paired binary-plist mirror path is retained.
- Server-driven slideshows and photo transition themes are not implemented.
- DMAP/cover-art payload handling has protocol hooks, but track metadata and
  cover-art display are not implemented or advertised.
- Music-card pause/resume controls the local player; DACP commands to control
  the sender (pause, next/previous track) are not implemented.
- Receiver-initiated photo-stop reverse events are not implemented.
- No TCP-interleaved RAOP, IPv6-only transport, AWDL, AirPlay 2 multi-room,
  multi-speaker sample-accurate synchronization or hardware clock-drift servo.
- DRM-protected media and Apple certification are outside this implementation.

Consequently this version must not be labeled “perfect AirPlay 1 compatibility.”
The implemented paths above have host and cross-build evidence; the remaining
paths and hardware matrix are explicit follow-up work.

## Device acceptance checklist

1. Launch the full SD package in application mode and confirm the receiver name
   and LAN IP on the home screen.
2. Play music from an AirPlay 1 sender for 30 minutes; verify left/right channels,
   volume, mute, pause/resume, track changes and reconnect.
3. Test packet loss, sender Wi-Fi reconnect, FLUSH during playback, and takeover
   between music, video, IPTV and DLNA.
4. Upload JPEG/PNG photos; move forward/backward to exercise cache hits; stop and
   reconnect from another sender.
5. Test unprotected URL/HLS playback and PIN-based mirroring with audio.
6. Check `+` exits cleanly, controller/touch operation, and both docked/handheld
   output. Record sender OS/app versions and any failure logs without identities
   or pairing secrets.
