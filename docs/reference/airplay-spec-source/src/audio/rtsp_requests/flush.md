# FLUSH

The `FLUSH` request stops the streaming.

<span class="ex">Example:</span> pause the audio stream

<div class="client_server">
<p>client &rarr; server</p>

```http
FLUSH rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 31
Session: 1
RTP-Info: seq=25009;rtptime=1148010660
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
RTP-Info: rtptime=1147914212
Server: AirTunes/130.14
CSeq: 31
```
</div>
