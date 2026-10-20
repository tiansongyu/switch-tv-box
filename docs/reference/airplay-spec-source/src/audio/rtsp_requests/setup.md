# SETUP

The `SETUP` request initializes a record session. It sends all the
necessary transport informations. Three UDP channels are setup:

| channel  | description                  |
|----------|------------------------------|
| server   | audio data                   |
| control  | sync and retransmit requests |
| timing   | master clock sync            |

<span class="ex">Example:</span> setup a record session

<div class="client_server">
<p>client &rarr; server</p>

```http
SETUP rtsp://fe80::217:f2ff:fe0f:e0f6/3413821438 RTSP/1.0
CSeq: 4
Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=6001;timing_port=6002
User-Agent: iTunes/10.6 (Macintosh; Intel Mac OS X 10.7.3) AppleWebKit/535.18.5
Client-Instance: 56B29BB6CB904862
DACP-ID: 56B29BB6CB904862
Active-Remote: 1986535575
```
</div>
<div class="server_client">
<p>server &rarr; client</p>

```http
RTSP/1.0 200 OK
Transport: RTP/AVP/UDP;unicast;mode=record;server_port=53561;control_port=63379;timing_port=50607
Session: 1
Audio-Jack-Status: connected
Server: AirTunes/130.14
CSeq: 4
```
</div>
