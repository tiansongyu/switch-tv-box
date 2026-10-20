# Events
## Photo

This event notifies a client that a photo session has ended. Then the
server can safely disconnect.

|key       |type    |description|
|----------|--------|-----------|
|category  |string  |`photo`    |
|sessionID |integer |session ID |
|state     |string  |`stopped`  |

<span class="ex">Example:</span> stop photo session

<div class="server_client">
<p>server &rarr; client</p>

```http
POST /event HTTP/1.1
Content-Type: text/x-apple-plist+xml
Content-Length: 277
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>category</key>
  <string>photo</string>
  <key>sessionID</key>
  <integer>38</integer>
  <key>state</key>
  <string>stopped</string>
</dict>
</plist>
```
</div>

<div class="client_server">
<p>client &rarr; server</p>

```http
HTTP/1.1 200 OK
Content-Length: 0
```
</div>

## Slideshow

Slideshow events are used to notify the server about the playback state.

|key         |type    |description                       |
|------------|--------|----------------------------------|
|category    |string  |`slideshow`                       |
|lastAssetID |integer |last asset ID                     |
|sessionID   |integer |session ID                        |
|state       |string  |`loading`, `playing` or `stopped` |

<span class="ex">Example:</span> slideshow is currently playing

<div class="server_client">
<p>server &rarr; client</p>

```http
POST /event HTTP/1.1
Content-Type: text/x-apple-plist+xml
Content-Length: 371
X-Apple-Session-ID: f1634b51-5cae-4384-ade5-54f4159a15f1
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>category</key>
  <string>slideshow</string>
  <key>lastAssetID</key>
  <integer>5</integer>
  <key>sessionID</key>
  <integer>4</integer>
  <key>state</key>
  <string>playing</string>
</dict>
</plist>
```
</div>
<div class="client_server">
<p>client &rarr; server</p>

```http
HTTP/1.1 200 OK
Content-Length: 0
```
</div>
