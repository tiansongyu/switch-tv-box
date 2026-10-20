# RECORD

The `RECORD` request starts the audio streaming. The `RTP-Info` header
contains the following parameters:

|name     |size    |description                  |
|---------|--------|-----------------------------|
| seq     | 16-bit | initial RTP sequence number |
| rtptime | 32-bit | initial RTP timestamp       |

<span class="ex">Example:</span> start audio stream

<div class="client_server">
<p>client &rarr; server</p>

```http
RECORD rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 5
Session: 1
Range: npt=0-
RTP-Info: seq=20857;rtptime=1146549156
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Audio-Latency: 2205
Server: AirTunes/130.14
CSeq: 5
```
</div>
