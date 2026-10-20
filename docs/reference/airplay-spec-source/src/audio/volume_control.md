# Volume Control

Audio volume can be changed using a `SET_PARAMETER` request. The volume
is a float value representing the audio attenuation in dB. A value of
&#8211;144 means the audio is muted. Then it goes from &#8211;30 to 0.

<span class="ex">Example:</span> set audio volume

<div class="client_server">
<p>client &rarr; server</p>

```http
SET_PARAMETER rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 6
Session: 1
Content-Type: text/parameters
Content-Length: 20
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575

volume: -11.123877
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 6
```
</div>
