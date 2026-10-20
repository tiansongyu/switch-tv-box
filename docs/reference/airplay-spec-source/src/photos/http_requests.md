# HTTP requests

## GET /slideshow-features

A client can fetch the list of available transitions for slideshows.
Then it can let the user pick one, before starting a slideshow. The
`Accept-Language` header is used to specify in which language the
transition names should be.

<div class="client_server">
<p>client &rarr; server</p>

```http
GET /slideshow-features HTTP/1.1
Accept-Language: English
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: cdda804c-33ae-4a0b-a5f2-f0e532fd5abd
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:41 GMT
Content-Type: text/x-apple-plist+xml
Content-Length: 6411
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>themes</key>
  <array>
    <dict>
      <key>key</key>
      <string>Reflections</string>
      <key>name</key>
      <string>Reflections</string>
    </dict>
    ...
  </array>
</dict>
</plist>
```
</div>

### PUT /photo

Send a JPEG picture to the server. The following headers are supported:

|name|description|
|----|-----------|
|`X-Apple-AssetKey`|UUID for the picture|
|`X-Apple-Transition`|transition that should be used to show the picture|
|`X-Apple-AssetAction`|specify a caching operation|

<span class="ex">Example 1:</span> show a picture without any transition (for the first time)

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /photo HTTP/1.1
X-Apple-AssetKey: F92F9B91-954E-4D63-BB9A-EEC771ADE6E8
Content-Length: 462848
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c

<JPEG DATA>
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:42 GMT
Content-Length: 0
```
</div>


<span class="ex">Example 2:</span> show a picture using the dissolve transition

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /photo HTTP/1.1
X-Apple-AssetKey: F92F9B91-954E-4D63-BB9A-EEC771ADE6E8
X-Apple-Transition: Dissolve
Content-Length: 462848
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c

<JPEG DATA>
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:42 GMT
Content-Length: 0
```
</div>

### PUT /slideshows/1

Start or stop a slideshow session. When starting, slideshow settings
such as the slide duration and selected transition theme are
transmitted. The following parameters are sent in an XML property list:

|key                    |type    |description                |
|-----------------------|--------|---------------------------|
|settings.slideDuration |integer | slide duration in seconds |
|settings.theme         |string  | selected transition theme |
|state                  |string  | `playing` or `stopped`    |

<span class="ex">Example:</span> send slideshow settings

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /slideshows/1 HTTP/1.1
Content-Type: text/x-apple-plist+xml
Content-Length: 366
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 98a7b246-8e00-49a6-8765-db57165f5b67
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>settings</key>
  <dict>
    <key>slideDuration</key>
    <integer>3</integer>
    <key>theme</key>
    <string>Classic</string>
  </dict>
  <key>state</key>
  <string>playing</string>
</dict>
</plist>
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 08 Mar 2012 16:30:01 GMT
Content-Type: text/x-apple-plist+xml
Content-Length: 181
```
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict/>
</plist>
```
</div>

### POST /stop

Stop a photo or slideshow session.

<div class="client_server">
<p>client &rarr; server</p>

```http
POST /stop HTTP/1.1
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:55 GMT
Content-Length: 0
```
</div>
