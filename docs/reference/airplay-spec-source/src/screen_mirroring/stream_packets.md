# Stream Packets

The video stream is packetized using 128-byte headers, followed by an
optional payload. Only the first 64 bytes of headers seem to be used.
Headers start with the following little-endian fields:

| size    | description              |
|---------|--------------------------|
| 4 bytes | payload size             |
| 2 bytes | payload type             |
| 2 bytes | 0x1e if type = 2, else 6 |
| 8 bytes | NTP timestamp            |

There are 3 types of packets:

| type | description     |
|------|-----------------|
| 0    | video bitstream |
| 1    | codec data      |
| 2    | heartbeat       |


## Codec Data

This packet contains the H.264 extra data in *avcC* format
([ISO/IEC 14496:15](http://www.iso.org/iso/iso_catalogue/catalogue_tc/catalogue_detail.htm?csnumber=55980)).
It is sent at the beginning of the stream, each time the
video properties might change, when screen orientation changes, and when
the screen is turned on or off.

<p class="caption">H.264 codec data from iPad</p>

```hex
0000   01 64 c0 28 ff e1 00 10 67 64 c0 28 ac 56 20 0d
0010   81 4f e5 9b 81 01 01 01 01 00 04 28 ee 3c b0
```

The H.264 codec data is interpreted as follows:

| size     | value   | description               |
|----------|---------|---------------------------|
| 1 byte   | 1       | version                   |
| 1 byte   | 100     | profile (high)            |
| 1 byte   | 0xc0    | compatibility             |
| 1 byte   | 40      | level (4.0)               |
| 6 bits   | 0x3f    | reserved                  |
| 2 bits   | 3       | NAL units length size - 1 |
| 3 bits   | 0x7     | reserved                  |
| 5 bits   | 1       | number of SPS             |
| 2 bytes  | 16      | length of SPS             |
| 16 bytes | &#8230; | Sequence parameter set    |
| 1 byte   | 1       | number of PPS             |
| 2 bytes  | 4       | length of PPS             |
| 4 bytes  | &#8230; | Picture parameter set     |

<p class="caption">Codec data packet from iPad</p>

```hex
0000   1f 00 00 00 01 00 06 00 1d 9a 9f 59 ef de 00 00
0010   00 00 58 44 00 00 22 44 00 00 00 00 00 00 00 00
0020   00 00 00 00 00 00 00 00 00 00 58 44 00 00 22 44
0030   00 00 50 43 00 00 10 42 00 c0 57 44 00 c0 21 44
0040   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0050   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0070   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0080   01 64 c0 28 ff e1 00 10 67 64 c0 28 ac 56 20 0d
0090   81 4f e5 9b 81 01 01 01 01 00 04 28 ee 3c b0
```


## Video Bitstream

This packet contains the video bitstream to be decoded. The payload can
be optionally AES encrypted. The NTP timestamp found in the header
serves as presentation timestamp.

<p class="caption">Video bitstream packet from iPad</p>

```hex
0000   c8 08 00 00 00 00 06 00 e9 e6 f5 ac 60 e0 00 00
0010   58 37 6e f9 40 01 00 00 00 00 00 00 00 00 00 00
0020   00 00 00 00 00 00 00 00 00 00 58 44 00 00 22 44
0030   00 00 50 43 00 00 10 42 00 c0 57 44 00 c0 21 44
0040   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0050   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0070   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0080   ...
```


## Heartbeat

Sent every second, this packet does not contain any payload.

<p class="caption">Heartbeat packet from iPad</p>

```hex
0000   00 00 00 00 02 00 1e 00 00 00 00 00 00 00 00 00
0010   4d d8 1a 41 00 00 00 00 00 00 20 41 86 c9 e2 36
0020   00 00 00 00 80 88 44 4b 00 00 00 00 00 00 00 00
0030   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0050   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0070   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
