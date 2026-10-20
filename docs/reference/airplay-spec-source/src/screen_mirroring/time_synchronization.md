# Time Synchronization

Time synchronization takes place on UDP ports 7010 (client) and 7011
(server), using the [NTP] protocol. The AirPlay server runs
an NTP client. Requests are sent to the AirPlay client at 3 second
intervals. The reference date for the timestamps is the beginning of the
mirroring session.

<div class="server_client">
<p>server &rarr; client</p>

```http
0000   23 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0010   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020   00 00 00 00 00 00 00 00 00 00 01 c4 c8 ac 5d b5

Network Time Protocol
Flags: 0x23
  00.. .... = Leap Indicator: no warning (0)
  ..10 0... = Version number: NTP Version 4 (4)
  .... .011 = Mode: client (3)
Peer Clock Stratum: unspecified or invalid (0)
Peer Polling Interval: invalid (0)
Peer Clock Precision: 1.000000 sec
Root Delay: 0.0000 sec
Root Dispersion: 0.0000 sec
Reference ID: NULL
Reference Timestamp: Jan 1, 1970 00:00:00.000000000 UTC
Origin Timestamp: Jan 1, 1970 00:00:00.000000000 UTC
Receive Timestamp: Jan 1, 1970 00:00:00.000000000 UTC
Transmit Timestamp: Jan 1, 1900 00:07:32.783880000 UTC
```
</div>
<div class="client_server">
<p>client &rarr; server</p>

```http
0000   24 01 02 e8 00 00 00 00 00 00 00 00 41 49 52 50
0010   00 00 00 00 00 00 00 00 00 00 01 c4 c8 ac 5d b5
0020   00 00 01 c4 c9 6a 0b a1 00 00 01 c4 c9 78 73 d2

Network Time Protocol
Flags: 0x24
  00.. .... = Leap Indicator: no warning (0)
  ..10 0... = Version number: NTP Version 4 (4)
  .... .100 = Mode: server (4)
Peer Clock Stratum: primary reference (1)
Peer Polling Interval: invalid (2)
Peer Clock Precision: 0.000000 sec
Root Delay: 0.0000 sec
Root Dispersion: 0.0000 sec
Reference ID: Unidentified reference source 'AIRP'
Reference Timestamp: Jan 1, 1970 00:00:00.000000000 UTC
Origin Timestamp: Jan 1, 1900 00:07:32.783880000 UTC
Receive Timestamp: Jan 1, 1900 00:07:32.786774000 UTC
Transmit Timestamp: Jan 1, 1900 00:07:32.786994000 UTC
```
</div>
