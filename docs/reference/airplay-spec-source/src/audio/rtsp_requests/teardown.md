# TEARDOWN

The `TEARDOWN` request ends the RTSP session.

<span class="ex">Example:</span> close session 1

<div class="client_server">
<p>client &rarr; server</p>

```http
TEARDOWN rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 32
Session: 1
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
Server: AirTunes/130.14
CSeq: 32
```
</div>
