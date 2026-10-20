# ANNOUNCE

The `ANNOUNCE` request tells the RTSP server about stream properties using
[SDP]. Codec informations and encryption keys are of particular interest.

<span class="ex">Example 1:</span> `ANNOUNCE` for *Apple Lossless* audio from iTunes

<div class="client_server">
<p>client &rarr; server</p>

```http
ANNOUNCE rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 3
Content-Type: application/sdp
Content-Length: 348
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575

v=0
o=iTunes 3413821438 0 IN IP4 fe80::217:f2ff:fe0f:e0f6
s=iTunes
c=IN IP4 fe80::5a55:caff:fe1a:e187
t=0 0
m=audio 0 RTP/AVP 96
a=rtpmap:96 AppleLossless
a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100
a=fpaeskey:RlBMWQECAQAAAAA8AAAAAPFOnNe+zWb5/n4L5KZkE2AAAAAQlDx69reTdwHF9LaNmhiRURTAbcL4brYAceAkZ49YirXm62N4
a=aesiv:5b+YZi9Ikb845BmNhaVo+Q
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 3
```
</div>

<span class="ex">Example 2:</span> `ANNOUNCE` for *AAC* audio from an iOS
device

<div class="client_server">
<p>client &rarr; server</p>

```http
ANNOUNCE rtsp://192.168.1.45/2699324803567405959 RTSP/1.0
X-Apple-Device-ID: 0xa4d1d2800b68
CSeq: 16
DACP-ID: 14413BE4996FEA4D
Active-Remote: 2543110914
Content-Type: application/sdp
Content-Length: 331

v=0
o=AirTunes 2699324803567405959 0 IN IP4 192.168.1.5
s=AirTunes
c=IN IP4 192.168.1.5
t=0 0
m=audio 0 RTP/AVP 96
a=rtpmap:96 mpeg4-generic/44100/2
a=fmtp:96
a=fpaeskey:RlBMWQECAQAAAAA8AAAAAOG6c4aMdLkXAX+lbjp7EhgAAAAQeX5uqGyYkBmJX+gd5ANEr+amI8urqFmvcNo87pR0BXGJ4eLf
a=aesiv:VZTaHn4wSJ84Jjzlb94m0Q==
a=min-latency:11025
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 16
```
</div>

<span class="ex">Example 3:</span> `ANNOUNCE` for *AAC-ELD* audio and *H.264*
video from an iOS device

<div class="client_server">
<p>client &rarr; server</p>

```http
ANNOUNCE rtsp://192.168.1.45/846700446248110360 RTSP/1.0
X-Apple-Device-ID: 0xa4d1d2800b68
CSeq: 27
DACP-ID: 14413BE4996FEA4D
Active-Remote: 2543110914
Content-Type: application/sdp
Content-Length: 415

v=0
o=AirTunes 846700446248110360 0 IN IP4 192.168.1.5
s=AirTunes
c=IN IP4 192.168.1.5
t=0 0
m=audio 0 RTP/AVP 96
a=rtpmap:96 mpeg4-generic/44100/2
a=fmtp:96 mode=AAC-eld; constantDuration=480
a=fpaeskey:RlBMWQECAQAAAAA8AAAAAKKp+t27A+686xfviEphhw8AAAAQE/3LSqv9MHgnEKxkbKh1buE9+ylKg0YuqcyAC7fT0EqJNtdq
a=aesiv:i/a3nUKYNDSIPP2fC+UKGQ==
a=min-latency:4410
m=video 0 RTP/AVP 97
a=rtpmap:97 H264
a=fmtp:97
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Server: AirTunes/130.14
CSeq: 27
```
</div>
