# Remote Control

Audio speakers can send commands to the AirPlay client to change the
current track, pause and resume playback, shuffle the playlist, and
more. This uses a subset of [DACP].
An AirPlay client advertises this capability by including a `DACP-ID` header in
its RTSP requests, with a 64-bit ID for the DACP server. An `Active-Remote`
header is included as well, serving as an authentication token.

The AirPlay server needs to browse the mDNS `_dacp._tcp` services for a
matching DACP server. Server names look like `iTunes_Ctrl_$ID`.

<p class="caption">DACP service from iTunes</p>

```
name: iTunes_Ctrl_56B29BB6CB904862
type: _dacp._tcp
port: 3689
txt:
txtvers=1
Ver=131075
DbId=63B5E5C0C201542E
OSsi=0x1F5
```

Once the DACP server has been identified, HTTP requests can be sent to the
corresponding service port. The `Active-Remote` header must be included in
these requests, so no additional pairing is required. The location for
remote control commands is `/ctrl-int/1/$CMD`. The following commands are
available:

| command       | description                       |
|---------------|-----------------------------------|
| beginff       | begin fast forward                |
| beginrew      | begin rewind                      |
| mutetoggle    | toggle mute status                |
| nextitem      | play next item in playlist        |
| previtem      | play previous item in playlist    |
| pause         | pause playback                    |
| playpause     | toggle between play and pause     |
| play          | start playback                    |
| stop          | stop playback                     |
| playresume    | play after fast forward or rewind |
| shuffle_songs | shuffle playlist                  |
| volumedown    | turn audio volume down            |
| volumeup      | turn audio volume up              |

<span class="ex">Example:</span> send a pause command

<div class="server_client">
<p>server &rarr; client</p>

```http
GET /ctrl-int/1/pause HTTP/1.1
Host: starlight.local.
Active-Remote: 1986535575
```
</div>
<div class="client_server">
<p>client &rarr; server</p>

```http
HTTP/1.1 204 No Content
Date: Tue, 06 Mar 2012 16:38:51 GMT
DAAP-Server: iTunes/10.6 (Mac OS X)
Content-Type: application/x-dmap-tagged
Content-Length: 0
```
</div>
