# Slideshows

Slideshows are using the reverse HTTP connection for asynchronous
loading of pictures. Three connections are performed in parallel. The
`X-Apple-Purpose` header is set to `slideshow`. A `GET` request to the
`/slideshows/1/assets/1` location is issued to fetch a new picture from
the AirPlay client. A binary property list with the following parameters
is expected as reply:

|key      |type    |description  |
|---------|--------|-------------|
|data     |data    |JPEG picture |
|info.id  |integer |asset ID     |
|info.key |integer |1            |

<span class="ex">Example:</span> fetch a new picture

<div class="server_client">
<p>server &rarr; client</p>

```http
GET /slideshows/1/assets/1 HTTP/1.1
Content-Length: 0
Accept: application/x-apple-binary-plist
X-Apple-Session-ID: 98a7b246-8e00-49a6-8765-db57165f5b67
```
</div>

<div class="client_server">
<p>client &rarr; server</p>

```http
HTTP/1.1 200 OK
Content-Type: application/x-apple-binary-plist
Content-Length: 58932

<BINARY PLIST DATA>
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>data</key>
  <data>
  ...
  </data>
  <key>info</key>
  <dict>
    <key>id</key>
    <integer>1</integer>
    <key>key</key>
    <string>1</string>
  </dict>
</dict>
</plist>
```
</div>
