# Get info

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
{{#include ../data/examples/Denon-AVR-X3500H/info.plist.xml}}
```

```base64
{{#include ../data/examples/Denon-AVR-X3500H/info.plist.base64}}
```
</div>
</div>
