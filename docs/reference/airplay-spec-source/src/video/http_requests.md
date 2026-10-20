# HTTP requests
## GET /server-info

Fetch general informations about the AirPlay server. These informations
are returned as an XML property list, with the following properties:

|key       |type    |value             |description      |
|----------|--------|------------------|-----------------|
|deviceid  |string  |58:55:CA:1A:E2:88 |MAC address      |
|features  |integer |14839             |0x39f7           |
|model     |string  |AppleTV2,1        |device model     |
|protovers |string  |1.0               |protocol version |
|srcvers   |string  |120.2             |server version   |

The `model`, `deviceid`, `srcvers` and `features` properties are the
same as broadcasted by the mDNS AirPlay service.

<span class="ex">Example:</span> fetch server informations

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /server-info HTTP/1.1
X-Apple-Device-ID: 0xdc2b61a0ce79
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:41 GMT
Content-Type: text/x-apple-plist+xml
Content-Length: 427
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>deviceid</key>
  <string>58:55:CA:1A:E2:88</string>
  <key>features</key>
  <integer>14839</integer>
  <key>model</key>
  <string>AppleTV2,1</string>
  <key>protovers</key>
  <string>1.0</string>
  <key>srcvers</key>
  <string>120.2</string>
</dict>
</plist>
```
</div>

## POST /play

Start video playback. The body contains the following parameters:

|name             |type  |description                       |
|-----------------|------|----------------------------------|
|Content-Location |URL   |URL for the video                 |
|Start-Position   |float |starting position between 0 and 1 |

MP4 movies are supported using progressive download. [HTTP Live Streaming]
might be supported as well, as indicated by the `VideoHTTPLiveStreams`
feature flag. The relative starting position, a float value between 0
(beginning) and 1 (end) is used to start playing a video at the exact same
position as it was on the client.

A binary property list can also be used instead of text parameters, with
content type `application/x-apple-binary-plist`.

<span class="ex">Example 1:</span> video playback from iTunes

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /play HTTP/1.1
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Content-Length: 163
Content-Type: text/parameters

Content-Location: http://192.168.1.18:3689/airplay.mp4?database-spec='dmap.persistentid:0x63b5e5c0c201542e'&item-spec='dmap.itemid:0x21d'
Start-Position: 0.174051
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 18:08:25 GMT
Content-Length: 0
```
</div>


<span class="ex">Example 2:</span> video playback from iPhone

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /play HTTP/1.1
X-Transmit-Date: 2012-03-16T14:20:39.656533Z
Content-Type: application/x-apple-binary-plist
Content-Length: 491
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 368e90a4-5de6-4196-9e58-9917bdd4ffd7

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Content-Location</key>
  <string>http://redirector.c.youtube.com/videoplayback?...</string>
  <key>Start-Position</key>
  <real>0.024613151326775551</real>
</dict>
</plist>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
```
</div>

## POST /scrub

Seek at an arbitrary location in the video. The <code>position</code> argument is a
float value representing the location in seconds.

<span class="ex">Example:</span> seek to about 20 seconds

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /scrub?position=20.097000 HTTP/1.1
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Content-Length: 0
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 18:08:42 GMT
Content-Length: 0
```
</div>

## POST /rate

Change the playback rate. The `value` argument is a float value
representing the playback rate: 0 is paused, 1 is playing at the normal
speed.

<span class="ex">Example:</span> pause playback

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /rate?value=0.000000 HTTP/1.1
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Content-Length: 0
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 18:08:37 GMT
Content-Length: 0
```
</div>

## POST /stop

Stop playback.

<span class="ex">Example:</span> stop playback

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /stop HTTP/1.1
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Content-Length: 0
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 18:09:06 GMT
Content-Length: 0
```
</div>

## GET /scrub

Retrieve the current playback position. This can be called repeatedly to
update a timeline on the client. The following parameters are returned:

|name     |type  |description         |
|---------|------|--------------------|
|duration |float |duration in seconds |
|position |float |position in seconds |

<span class="ex">Example:</span> fetch current playback progress

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /scrub HTTP/1.1
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Content-Length: 0
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 18:08:31 GMT
Content-Type: text/parameters
Content-Length: 40

duration: 83.124794
position: 14.467000
```
</div>

## GET /playback-info

Retrieve playback informations such as position, duration, rate,
buffering status and more. An XML property list is returned with the
following parameters:

|key                    |type    |description                   |
|-----------------------|--------|------------------------------|
|duration               |real    |playback duration in seconds  |
|position               |real    |playback position in seconds  |
|rate                   |real    |playback rate                 |
|readyToPlay            |boolean |ready to play                 |
|playbackBufferEmpty    |boolean |buffer empty                  |
|playbackBufferFull     |boolean |buffer full                   |
|playbackLikelyToKeepUp |boolean |playback likely to keep up    |
|loadedTimeRanges       |array   |array of loaded time ranges   |
|seekableTimeRanges     |array   |array of seekable time ranges |

Ranges are defined as dictionaries with the following keys:

|key      |type |description                 |
|---------|-----|----------------------------|
|start    |real |range start time in seconds |
|duration |real |range duration in seconds   |

<span class="ex">Example:</span> get playback info

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /playback-info HTTP/1.1
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 24b3fd94-1b6d-42b1-89a3-47108bfbac89
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Fri, 16 Mar 2012 15:31:42 GMT
Content-Type: text/x-apple-plist+xml
Content-Length: 801
X-Transmit-Date: 2012-03-16T15:31:42.607066Z
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>duration</key><real>1801</real>
  <key>loadedTimeRanges</key>
  <array>
    <dict>
      <key>duration</key> <real>51.541130402</real>
      <key>start</key> <real>18.118717650000001</real>
    </dict>
  </array>
  <key>playbackBufferEmpty</key> <true/>
  <key>playbackBufferFull</key> <false/>
  <key>playbackLikelyToKeepUp</key> <true/>
  <key>position</key> <real>18.043869775000001</real>
  <key>rate</key> <real>1</real>
  <key>readyToPlay</key> <true/>
  <key>seekableTimeRanges</key>
  <array>
    <dict>
      <key>duration</key>
      <real>1801</real>
      <key>start</key>
      <real>0.0</real>
    </dict>
  </array>
</dict>
</plist>
```
</div>

## PUT /setProperty

Set playback property. The property name is sent as query argument. The
following properties are defined:

|argument       |description      |
|---------------|-----------------|
|forwardEndTime |forward end time |
|reverseEndTime |reverse end time |

<span class="ex">Example:</span> set forward end time

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /setProperty?forwardEndTime HTTP/1.1
Content-Type: application/x-apple-binary-plist
Content-Length: 96
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 24b3fd94-1b6d-42b1-89a3-47108bfbac89

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>value</key>
  <dict>
    <key>epoch</key> <integer>0</integer>
    <key>flags</key> <integer>0</integer>
    <key>timescale</key> <integer>0</integer>
    <key>value</key> <integer>0</integer>
  </dict>
</dict>
</plist>
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Fri, 16 Mar 2012 15:23:11 GMT
Content-Type: application/x-apple-binary-plist
Content-Length: 58

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>errorCode</key>
  <integer>0</integer>
</dict>
</plist>
```
</div>

## GET /getProperty

Get playback property. The property name is sent as query argument. The
following properties are defined:

|argument          |description         |
|------------------|--------------------|
|playbackAccessLog |playback access log |
|playbackErrorLog  |playback error log  |

<span class="ex">Example:</span> get playback access log

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /getProperty?playbackAccessLog HTTP/1.1
Content-Type: application/x-apple-binary-plist
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 24b3fd94-1b6d-42b1-89a3-47108bfbac89
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Fri, 16 Mar 2012 15:31:42 GMT
Content-Type: application/x-apple-binary-plist
Content-Length: 530

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>errorCode</key>
  <integer>0</integer>
  <key>value</key>
  <array>
    <dict>
      <key>bytes</key> <integer>1818336</integer>
      <key>c-duration-downloaded</key> <real>70</real>
      <key>c-duration-watched</key> <real>18.154102027416229</real>
      <key>c-frames-dropped</key> <integer>0</integer>
      <key>c-observed-bitrate</key> <real>14598047.302367469</real>
      <key>c-overdue</key> <integer>0</integer>
      <key>c-stalls</key> <integer>0</integer>
      <key>c-start-time</key> <real>0.0</real>
      <key>c-startup-time</key> <real>0.27732497453689575</real>
      <key>cs-guid</key> <string>B475F105-78FD-4200-96BC-148BAB6DAC11</string>
      <key>date</key> <date>2012-03-16T15:31:24Z</date>
      <key>s-ip</key> <string>213.152.6.89</string>
      <key>s-ip-changes</key> <integer>0</integer>
      <key>sc-count</key> <integer>7</integer>
      <key>uri</key> <string>http://devimages.apple.com/iphone/samples/bipbop/gear1/prog_index.m3u8</string>
    </dict>
  </array>
</dict>
</plist>
```
</div>
