# FLUSHBUFFERED

Applies to: Airplay2

The `FLUSHBUFFERED` is used to request the flush of the buffer, in a buffered audio usage context (see SupportsBufferedAudio feature)

<div class="client_server">
<p>client &rarr; server</p>

```http
FLUSHBUFFERED rtsp://192.168.128.227/4361780817803627124 RTSP/1.0
Content-Length: 87
Content-Type: application/x-apple-binary-plist
CSeq: 12
DACP-ID: C0EF6588CF6D70AC
Active-Remote: 1641836738
User-Agent: AirPlay/387.2

<binary plist>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/366.0
CSeq: 12
```
</div>

binary plist includes the RTP sequence & timestamp to flush up to:
```
{'flushUntilSeq': 15363674, 'flushUntilTS': 239565966}
```
and optionally, the RTP sequence & timestamp to flush from:
```
{'flushFromSeq': 15380707,
 'flushFromTS': 1554930586,
 'flushUntilSeq': 15381800,
 'flushUntilTS': 1556050842}
 ```
