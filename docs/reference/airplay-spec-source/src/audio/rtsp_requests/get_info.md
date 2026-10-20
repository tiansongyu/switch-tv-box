# GET /info

|key       |type    |value             |description      |
|----------|--------|------------------|-----------------|
|PTPInfo|string|
|build|string|
|deviceID  |string  |58:55:CA:1A:E2:88 |MAC address      |
|features  |integer |14839             | [features](../../features.md) bits as decimal value |
|initialVolume|real|||
|macAddress|string|||
|firmwareBuildDate|string|||
|firmwareRevision|string|||
|keepAliveLowPower|boolean|||
|keepAliveSendStatsAsBody|boolean|||
|manufacturer|string||
|model     |string  |AppleTV2,1        |device model     |
|name|string|||
|nameIsFactoryDefault|boolean|||
|pi|string|||
|pk|data|||
|playbackCapabilities.supportsFPSSecureStop|boolean|||
|playbackCapabilities.supportsUIForAudioOnlyContent|boolean|||
|protocolVersion |string  |1.0               |protocol version |
|psi|string|
|senderAddress|string|
|sdk|string|
|sourceVersion   |string  |120.2             |server version   |
|statusFlags|integer|4| [status flags](../../status_flags.md) as decimal value |
|txtAirPlay|data|...|raw TXT record from [AirPlay service](../../service_discovery.md) mDNS record |
|txtRAOP|data|...|raw TXT record from [AirTunes service](../../service_discovery.md) mDNS record |
|volumeControlType|integer|
|vv|integer|

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /info RTSP/1.0
X-Apple-ProtocolVersion: 1
CSeq: 0
DACP-ID: ADA239F4521B1802
Active-Remote: 753030410
User-Agent: AirPlay/380.10.1
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Content-Length: 927
Content-Type: application/x-apple-binary-plist
Server: AirTunes/366.0
CSeq: 0

<BINARY PLIST DATA>
```
<div class="plist">

```xml
{{#include ../../data/examples/Denon-AVR-X3500H/info.plist.xml}}
```

```base64
{{#include ../../data/examples/Denon-AVR-X3500H/info.plist.base64}}
```
</div>
</div>
