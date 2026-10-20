# Photo Caching

AirPlay supports preloading picture data to improve transition latency.
This works by preloading a few pictures (most likely the ones before and
after the current picture) just after displaying one.

Preloading is achieved using the `cacheOnly` asset action. Upon
receiving this request, a server stores the picture in its cache. Later,
a client can request the display of this picture using the
`displayCached` asset action and the same asset key. This is much faster
than a full picture upload because no additional data is transmitted.

When asked for a picture which is no longer in the cache, a server
replies with an HTTP 412 error code (Precondition Failed).

<span class="ex">Example 1:</span> cache a picture for future display

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /photo HTTP/1.1
X-Apple-AssetAction: cacheOnly
X-Apple-AssetKey: B0DDE2C0-6FDD-48F8-9E5B-29CE0618DF5B
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
Date: Thu, 23 Feb 2012 17:33:45 GMT
Content-Length: 0
```
</div>

<span class="ex">Example 2:</span> show a cached picture

<div class="client_server">
<p>client &rarr; server</p>

```http
PUT /photo HTTP/1.1
X-Apple-AssetAction: displayCached
X-Apple-AssetKey: B0DDE2C0-6FDD-48F8-9E5B-29CE0618DF5B
X-Apple-Transition: Dissolve
Content-Length: 0
User-Agent: MediaControl/1.0
X-Apple-Session-ID: 1bd6ceeb-fffd-456c-a09c-996053a7a08c
```
</div>

<div class="server_client">
<p>server &rarr; client</p>

```http
HTTP/1.1 200 OK
Date: Thu, 23 Feb 2012 17:33:45 GMT
Content-Length: 0
```
</div>
