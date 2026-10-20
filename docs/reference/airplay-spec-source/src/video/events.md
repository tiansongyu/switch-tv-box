# Events

This event is used to send the playback state to the client:

|key       |type    |description                                |
|----------|--------|-------------------------------------------|
|category  |string  |`video`                                    |
|sessionID |integer |session id                                 |
|state     |string  |`loading`, `playing`, `paused` or `stopped` |

<span class="ex">Example:</span> notify the client that video playback is paused

<div class="server_client">
<p>server &rarr; client</p>

```http
POST /event HTTP/1.1
Content-Type: application/x-apple-plist
Content-Length: 321
X-Apple-Session-ID: 00000000-0000-0000-0000-000000000000
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>category</key>
  <string>video</string>
  <key>sessionID</key>
  <integer>13</integer>
  <key>state</key>
  <string>paused</string>
</dict>
</plist>
```
</div>

<div class="client_server">
<p>client &rarr; server</p>

```http
HTTP/1.1 200 OK
Content-Length: 0
Date: Mon, 08 Mar 2012 18:07:43 GMT
```
</div>
