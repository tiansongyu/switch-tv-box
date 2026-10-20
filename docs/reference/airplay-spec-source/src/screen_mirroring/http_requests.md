# HTTP requests

Screen mirroring does not use the standard AirPlay service. Instead it
connects to an apparently hard-coded port 7100. This is a HTTP server
which supports the following requests:

## GET /stream.xml

Retrieve information about the server capabilities. The server sends an
XML property list with the following properties:

| key         | type    | value          | description                 |
|-------------|---------|----------------|-----------------------------|
| height      | integer | 720            | vertical resolution         |
| width       | integer | 1280           | horizontal resolution       |
| overscanned | boolean | true           | is the display overscanned? |
| refreshRate | real    | 0.01666&#8230; | refresh rate 60 Hz (1/60)   |
| version     | string  | 130.14         | server version              |

These properties tell us that the AirPlay server is connected to a
1280x720, 60 Hz, overscanned display.

<span class="ex">Example:</span> fetch mirroring server informations

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /stream.xml HTTP/1.1
Content-Length: 0
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Mon, 08 Mar 2012 15:30:27 GMT
Content-Type: text/x-apple-plist+xml
Content-Length: 411
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
"http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>height</key>
  <integer>720</integer>
  <key>overscanned</key>
  <true/>
  <key>refreshRate</key>
  <real>0.016666666666666666</real>
  <key>version</key>
  <string>130.14</string>
  <key>width</key>
  <integer>1280</integer>
</dict>
</plist>
```
</div>


## POST /stream

Start the live video transmission. The client sends a binary property
list with information about the stream, immediately followed by the
stream itself. At this point, the connection is no longer a valid HTTP
connection.

The following parameters are sent:

| key           | type    | value            | description                      |
|---------------|---------|------------------|----------------------------------|
| deviceID      | integer | 181221086727016  | MAC address (A4:D1:D2:80:0B:68)  |
| sessionID     | integer | &#8211;808788724 | session ID (0xcfcadd0c)          |
| version       | string  | 130.16           | server version                   |
| param1        | data    | (72 bytes)       | AES key, encrypted with FairPlay |
| param2        | data    | (16 bytes)       | AES initialization vector        |
| latencyMs     | integer | 90               | video latency in ms              |
| fpsInfo       | array   |||
| timestampInfo | array   |||

The `param1` and `param2` parameters are optional.

As soon as the server receives a `/stream` request, it will send NTP
requests to the client on port 7010, which seems hard-coded as well. The
client needs to export its master clock there, which will be used for
audio/video synchronization and clock recovery.

<span class="ex">Example:</span> send stream information

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /stream HTTP/1.1
X-Apple-Device-ID: 0xa4d1d2800b68
Content-Length: 503

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
"http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>deviceID</key>
  <integer>181221086727016</integer>
  <key>fpsInfo</key>
  <array>
    <dict> <key>name</key> <string>SubS</string> </dict>
    <dict> <key>name</key> <string>B4En</string> </dict>
    <dict> <key>name</key> <string>EnDp</string> </dict>
    <dict> <key>name</key> <string>IdEn</string> </dict>
    <dict> <key>name</key> <string>IdDp</string> </dict>
    <dict> <key>name</key> <string>EQDp</string> </dict>
    <dict> <key>name</key> <string>QueF</string> </dict>
    <dict> <key>name</key> <string>Sent</string> </dict>
  </array>
  <key>latencyMs</key>
  <integer>90</integer>
  <key>param1</key>
  <data>
    RlBMWQECAQAAAAA8AAAAANvKuDizduszL1hG9IvIk+AAAAAQukdPJ5Jw/gGBAl22WZdF
    m9ujZEGIV7jm3ZByWm51HjpDwjYY
  </data>
  <key>param2</key>
  <data>
    3qpOHtYWbBPyEWPnGt1BuQ==
  </data>
  <key>sessionID</key>
  <integer>-808788724</integer>
  <key>timestampInfo</key>
  <array>
    <dict> <key>name</key> <string>SubSu</string> </dict>
    <dict> <key>name</key> <string>BePxT</string> </dict>
    <dict> <key>name</key> <string>AfPxT</string> </dict>
    <dict> <key>name</key> <string>BefEn</string> </dict>
    <dict> <key>name</key> <string>EmEnc</string> </dict>
    <dict> <key>name</key> <string>QueFr</string> </dict>
    <dict> <key>name</key> <string>SndFr</string> </dict>
  </array>
  <key>version</key>
  <string>130.16</string>
</dict>
</plist>
```
</div>
