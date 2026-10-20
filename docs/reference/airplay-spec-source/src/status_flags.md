# Status Flags

|bit|name|description|
|---|----|-----------|
|  0|Problem has been detected|Defined in CarPlay section of MFi spec. Not seen set anywhere|
|  1|Device is not configured|Defined in CarPlay section of MFi spec. Not seen set anywhere|
|  2|Audio cable is attached|Defined in CarPlay section of MFi spec. Seen on AppleTV, Denon AVR, HomePod, Airport Express|
|  3|PINRequired||
|  4|???|Not seen set anywhere|
|  5|???|Not seen set anywhere|
|  6|SupportsAirPlayFromCloud|
|  7|PasswordRequired||
|  8|???|Not seen set anywhere|
|  9|OneTimePairingRequired||
| 10|DeviceWasSetupForHKAccessControl||
| 11|DeviceSupportsRelay|Shows in logs as relayable. When set iOS will connect to the device to get currently playing track.|
| 12|SilentPrimary||
| 13|TightSyncIsGroupLeader||
| 14|TightSyncBuddyNotReachable||
| 15|IsAppleMusicSubscriber|Shows in logs as music|
| 16|CloudLibraryIsOn|Shows in logs as iCML|
| 17|ReceiverSessionIsActive|Shows in logs as airplay-receiving. Set when Apple TV is receiving anything via AirPlay.|
| 18|???|Not seen set anywhere|
| 19|???|Not seen set anywhere|
