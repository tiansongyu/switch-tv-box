# OPTIONS

The `OPTIONS` request asks the RTSP server for its supported methods.
Apple TV supports the following methods: `ANNOUNCE`, `SETUP`, `RECORD`,
`PAUSE`, `FLUSH`, `TEARDOWN`, `OPTIONS`, `GET_PARAMETER`, `SET_PARAMETER`,
`POST` and `GET`.

<div class="client_server">
<p>client &rarr; server</p>

```http
OPTIONS * RTSP/1.0
CSeq: 3
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
Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS,
GET_PARAMETER, SET_PARAMETER, POST, GET
Server: AirTunes/130.14
CSeq: 3
```
</div>
