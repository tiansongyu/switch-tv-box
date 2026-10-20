# RTP Streams

Audio packets are fully RTP compliant. Control and timing packets,
however, do not seem to be fully compliant with the RTP standard.

The following payload types are defined:

| payload type | port         | description        |
|--------------|--------------|--------------------|
| 82           | timing_port  | timing request     |
| 83           | timing_port  | timing reply       |
| 84           | control_port | time sync          |
| 85           | control_port | retransmit request |
| 86           | control_port | retransmit reply   |
| 96           | server_port  | audio data         |

## Audio packets

Audio data is sent using the `DynamicRTP-Type-96` payload type. The `Marker`
bit is set on the first packet sent after `RECORD` or `FLUSH` requests. The RTP
payload contains optionally encrypted audio data.

<span class="ex">Example:</span> encrypted audio packet

<div class="client_server">
<p>client &rarr; server</p>

```hex
0000   80 e0 b1 91 f7 79 16 c2 e8 bb 6b 2c bb 5c 8e 51
0010   aa 7c d2 96 00 c3 fd 60 eb ae 6e 41 31 38 fe ae
....
03e0   cb 1c 73 bf e7 05 93 30 fa 85 7f 32 77 8d a8 97
03f0   a0 c7 c8 78 7b e5 81 a1 4f b4 3e a3 43 db 7c

Real-Time Transport Protocol
10.. .... = Version: RFC 1889 Version (2)
..0. .... = Padding: False
...0 .... = Extension: False
.... 0000 = Contributing source identifiers count: 0
1... .... = Marker: True
Payload type: DynamicRTP-Type-96 (96)
Sequence number: 45457
Timestamp: 4151908034
Synchronization Source identifier: 0xe8bb6b2c (3904596780)
Payload: bb5c8e51aa7cd29600c3fd60ebae6e413138feae909b44f1...
```
</div>

## Sync packets

Sync packets are sent once per second to the control port. They are used to
correlate the RTP timestamps currently used in the audio stream to the NTP time
used for clock synchronization. Payload type is 84, the `Marker` bit is always
set and the `Extension` bit is set on the first packet after `RECORD` or
`FLUSH` requests. The `SSRC` field is not included in the RTP header.

| bytes | description                             |
|-------|-----------------------------------------|
| 8     | RTP header without `SSRC`               |
| 8     | current NTP time                        |
| 4     | RTP timestamp for the next audio packet |


<span class="ex">Example:</span> sync packet

<div class="client_server">
<p>client &rarr; server</p>

```hex
0000   80 d4 00 04 c7 cd 11 a8 83 ab 1c 49 2f e4 22 e2
0010   c7 ce 3f 1f

Real-Time Transport Protocol
10.. .... = Version: RFC 1889 Version (2)
..0. .... = Padding: False
...0 .... = Extension: False
.... 0000 = Contributing source identifiers count: 0
1... .... = Marker: True
Payload type: Unassigned (84)
Sequence number: 4
Timestamp: 3352105384
Synchronization Source identifier: 0x83ab1c49 (2209029193)
Payload: 2fe422e2c7ce3f1f
```
</div>

## Retransmit packets

AirTunes supports resending audio packets which have been lost. Payload
type is 85 for retransmit queries, the `Marker` bit is always set and the
`SSRC` field is not included in the RTP header.

| bytes | description                               |
|-------|-------------------------------------------|
| 8     | RTP header without `SSRC`                 |
| 2     | sequence number for the first lost packet |
| 2     | number of lost packets                    |

Retransmit replies have payload type 86, with a full audio RTP packet
after the sequence number.

## Timing packets

Timing packets are used to synchronize a master clock for audio. This is
useful for clock recovery and precise synchronization of several devices
playing the same audio stream.

Timing packets are sent at 3 second intervals. They always have the
`Marker` bit set, and payload type 82 for queries and 83 for replies.
The `SSRC` field is not included in the RTP header, so it takes only 8
bytes, followed by three *NTP* timestamps:

| bytes | description               |
|-------|---------------------------|
| 8     | RTP header without `SSRC` |
| 8     | origin timestamp          |
| 8     | receive timestamp         |
| 8     | transmit timestamp        |

<span class="ex">Example:</span> timing query/reply

<div class="server_client">
<p>server &rarr; client</p>

```hex
0000   80 d2 00 07 00 00 00 00 00 00 00 00 00 00 00 00
0010   00 00 00 00 00 00 00 00 83 c1 17 cc af ba 9b 32

Real-Time Transport Protocol
10.. .... = Version: RFC 1889 Version (2)
..0. .... = Padding: False
...0 .... = Extension: False
.... 0000 = Contributing source identifiers count: 0
1... .... = Marker: True
Payload type: Unassigned (82)
Sequence number: 7
Timestamp: 0
Synchronization Source identifier: 0x00000000 (0)
Payload: 00000000000000000000000083c117ccafba9b32
```
</div>
<div class="client_server">
<p>client &rarr; server</p>

```hex
0000   80 d3 00 07 00 00 00 00 83 c1 17 cc af ba 9b 32
0010   83 c1 17 cc b0 12 ce b6 83 c1 17 cc b0 14 10 47

Real-Time Transport Protocol
10.. .... = Version: RFC 1889 Version (2)
..0. .... = Padding: False
...0 .... = Extension: False
.... 0000 = Contributing source identifiers count: 0
1... .... = Marker: True
Payload type: Unassigned (83)
Sequence number: 7
Timestamp: 0
Synchronization Source identifier: 0x83c117cc (2210469836)
Payload: afba9b3283c117ccb012ceb683c117ccb0141047
```
</div>
