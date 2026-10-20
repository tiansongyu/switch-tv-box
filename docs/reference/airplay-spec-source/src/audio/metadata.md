# Metadata

Metadata for the current track are sent using `SET_PARAMETER` requests.
This allows the Apple TV to show the track name, artist, album, cover
artwork and timeline. The `RTP-Info` header contains a `rtptime` parameter
with the RTP timestamp corresponding to the time from which the metadata
is valid.

| md | bit | description |
|----|-----|-------------|
|  0 |  17 | text        |
|  1 |  15 | artwork     |
|  2 |  16 | progress    |
|    |  50 | bplist      |


## Track Informations

Informations about the current track are sent in the [DAAP] format, with
`application/x-dmap-tagged` content type.

The following DAAP attributes are displayed on Apple TV:

| attributes      | description |
|-----------------|-------------|
| dmap.itemname   | track name  |
| daap.songartist | artist      |
| daap.songalbum  | album       |

<span class="ex">Example:</span> send track informations

<div class="client_server">
<p>client &rarr; server</p>

```http
SET_PARAMETER rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 8
Session: 1
Content-Type: application/x-dmap-tagged
Content-Length: 3242
RTP-Info: rtptime=1146549156
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575

<DMAP DATA>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 8
```
</div>


## Cover Artwork

Artworks are sent as *JPEG* pictures, with `image/jpeg` content type.

<span class="ex">Example:</span> send cover artwork

<div class="client_server">
<p>client &rarr; server</p>

```http
SET_PARAMETER rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 9
Session: 1
Content-Type: image/jpeg
Content-Length: 34616
RTP-Info: rtptime=1146549156
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575

<JPEG DATA>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 9
```
</div>


## Playback Progress

Playback progress is sent as `text/parameters`, with a `progress` parameter
representing three absolute RTP timestamps values:
`start` / `curr` / `end`.

| timestamp | description                    |
|-----------|--------------------------------|
| start     | beginning of the current track |
| curr      | current playback position      |
| end       | end of the current track       |

The relative position and track duration can be computed as follows:

- `position = rtptime_to_sec(curr - start)`
- `duration = rtptime_to_sec(end - start)`

<span class="ex">Example:</span> send playback progress

<div class="client_server">
<p>client &rarr; server</p>

```http
SET_PARAMETER rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 10
Session: 1
Content-Type: text/parameters
Content-Length: 44
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3)
AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575

progress: 1146221540/1146549156/1195701740
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 10
```
</div>
