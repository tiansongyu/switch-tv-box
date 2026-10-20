# Service Discovery

AirPlay does not require any configuration to be able to find compatible
devices on the network, thanks to [DNS-based service discovery], based
on [Multicast DNS], aka *Bonjour*.

An AirPlay device such as the Apple TV publishes two services.
The first one is the *Airplay* for photo and video
and in later versions also audio content and the other one is [RAOP], used for audio streaming and in later versions superseded by *Airplay* service.


## _airplay._tcp

```
name: Apple TV
type: _airplay._tcp
port: 7000
txt:
  deviceid=58:55:CA:1A:E2:88
  features=0x39f7
  model=AppleTV2,1
  srcvers=130.14
```

The following fields are available in the TXT record:

|name|type|description|
|----|----|-----------|
|model|string|device model|
|manufacturer|string|device manufacturer|
|serialNumber|string|device serial number|
|fv|string|device firmware version|
|osvers|string|device OS version|
|deviceid|string|Device ID. Usually MAC address of the device|
|features|32 bit hex number,optional high order 32 bit hex number| bitfield of supported [features](./features.md). This was originally a 32 bit value but it has since been expanded to a 64 bit value. To support both these types the mDNS value is encoded as two 32 bit values separated by comma with the comma and second 32 bit value being optional.|
|pw|boolean|server is password protected|
|acl|int64|Access control level|
|srcvers|string|airplay version|
|flags|20 bit hex number|bitfield of [status flags](./status_flags.md)|
|pk|hex string|public key|
|pi|UUID string|group_id / PublicCUAirPlayPairingIdentifier|
|psi|UUID string|PublicCUSystemPairingIdentifier|
|gid|UUID string|group UUID|
|gcgl|boolean|group contains group leader / Group contains discoverable leader|
|igl|boolean|is group leader|
|gpn|string|group public name|
|hgid|UUID string|home group UUID|
|hmid|string|household ID|
|pgcgl|boolean|parent group contains discoverable leader|
|pgid|UUID string|parent group UUID|
|tsid|UUID string|3008B5C8-9BD3-4479-A564-90BFB3D780C0|tight sync UUID|
|rsf|64 bit hex number|required sender features|
|protovers|string|protocol version|
|vv|?|vodka version</td>


## _raop._tcp

```
name: 5855CA1AE288@Apple TV
type: _raop._tcp
port: 49152
txt:
  txtvers=1
  ch=2
  cn=0,1,2,3
  da=true
  et=0,3,5
  md=0,1,2
  pw=false
  sv=false
  sr=44100
  ss=16
  tp=UDP
  vn=65537
  vs=130.14
  am=AppleTV2,1
  sf=0x4
```

The name is formed using the MAC address of the device and the name of
the remote speaker which will be shown by the clients.

The following fields appear in the TXT record:

|name|value|description|
|----|-----|-----------|
|txtvers|1|TXT record version 1|
|ch|2|audio channels: stereo|
|cn|0,1,2,3|audio codecs|
|et|0,3,5|supported encryption types|
|md|0,1,2|supported metadata types|
|pw|false|does the speaker require a password?|
|sr|44100|audio sample rate: 44100 Hz|
|ss|16|audio sample size: 16-bit|
|tp|UDP|supported transport: TCP or UDP|
|vs|130.14|server version 130.14|
|am|AppleTV2,1|device model|

### Audio codecs

|cn|description                 |
|--|----------------------------|
| 0|PCM                         |
| 1|Apple Lossless (ALAC)       |
| 2|AAC                         |
| 3|AAC ELD (Enhanced Low Delay)|


### Encryption/Authentication Types

|et|description               |
|--|--------------------------|
| 0|no encryption             |
| 1|RSA (AirPort Express)     |
| 3|FairPlay                  |
| 4|MFiSAP (3rd-party devices)|
| 5|FairPlay SAPv2.5          |


### Metadata Types

|md|bit|description|
|--|---|-----------|
| 0| 17|text       |
| 1| 15|artwork    |
| 2| 16|progress   |
|  | 50|bplist     |


### Subtype

```
AppleTV = deviceModel has prefix “AppleTV”
HomePod = deviceModel has prefix “AudioAccessory”
ThirdPartySpeaker = HasUnifiedAdvertiserInfo or SupportsUnifiedPairSetupAndMFi feature is set
Unknown = otherwise
```


### CanBeRemoteControlled

`SupportsBufferedAudio` is set and `PINRequired` is not set


### State Changes

Depending on the state of the device the mDNS record is changed to reflect
this. Primarily it is the `flags`, `gid`, `igl`, `gcgl`, `pgid` and `pgcgl`
fields that are changed.


#### Apple TV not playing
```
flags=0x10644
gid=712F0759-5D44-41E7-AB67-FAB0AD39E165
igl=1
gcgl=1
```

#### Apple TV receiving AirPlay audio
```
flags=0x30e44
gid=19F5D4B2-8A06-4792-923E-8AFA83913238
igl=0
gcgl=0
pgid=19F5D4B2-8A06-4792-923E-8AFA83913238
pgcgl=0
```

#### Apple TV receiving AirPlay video
```
flags=0x30e44
gid=19F5D4B2-8A06-4792-923E-8AFA83913238
igl=0
gcgl=0
pgid=19F5D4B2-8A06-4792-923E-8AFA83913238
pgcgl=0
```

#### Apple TV receiving AirPlay screen
```
flags=0x30644
gid=712F0759-5D44-41E7-AB67-FAB0AD39E165
igl=1
gcgl=1
```

#### Apple TV playing local media
```
flags=0x10644
gid=FA69F5A2-5574-4E4D-A676-CA7F61A904A3
igl=1
gcgl=1
```
