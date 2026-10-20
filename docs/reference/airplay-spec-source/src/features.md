# Features

|bit|   name|description|
|---|-------|-----------|
|  0|Video|video supported|
|  1|Photo|photo supported|
|  2|VideoFairPlay|video protected with FairPlay DRM|
|  3|VideoVolumeControl|volume control supported for videos|
|  4|VideoHTTPLiveStreams|http live streaming supported|
|  5|Slideshow|slideshow supported|
|  6|||
|  7|Screen|mirroring supported|
|  8|ScreenRotate|screen rotation supported|
|  9|Audio|audio supported|
| 10|||
| 11|AudioRedundant|audio packet redundancy supported|
| 12|FPSAPv2pt5_AES_GCM|FairPlay secure auth supported|
| 13|PhotoCaching|photo preloading supported|
| 14|Authentication4|Authentication type 4. FairPlay authentication|
| 15|MetadataFeature1|bit 1 of MetadataFeatures. Artwork.|
| 16|MetadataFeature2|bit 2 of MetadataFeatures. Progress. |
| 17|MetadataFeature0|bit 0 of MetadataFeatures. Text. |
| 18|AudioFormat1|support for audio format 1|
| 19|AudioFormat2|support for audio format 2. This bit must be set for AirPlay 2 connection to work|
| 20|AudioFormat3|support for audio format 3. This bit must be set for AirPlay 2 connection to work|
| 21|AudioFormat4|support for audio format 4|
| 22|||
| 23|Authentication1|Authentication type 1. RSA Authentication|
| 24|||
| 25|||
| 26|HasUnifiedAdvertiserInfo||
| 27|SupportsLegacyPairing||
| 28|||
| 29|||
| 30|RAOP|RAOP is supported on this port. With this bit set your don't need the AirTunes service|
| 31|||
| 32|IsCarPlay / SupportsVolume|Don’t read key from `pk` record it is known|
| 33|SupportsAirPlayVideoPlayQueue||
| 34|SupportsAirPlayFromCloud||
| 35|||
| 36|||
| 37|||
| 38|SupportsCoreUtilsPairingAndEncryption| `SupportsHKPairingAndAccessControl`, `SupportsSystemPairing` and `SupportsTransientPairing` implies `SupportsCoreUtilsPairingAndEncryption`|
| 39|||
| 40|SupportsBufferedAudio|Bit needed for device to show as supporting multi-room audio|
| 41|SupportsPTP|Bit needed for device to show as supporting multi-room audio|
| 42|SupportsScreenMultiCodec||
| 43|SupportsSystemPairing||
| 44|||
| 45|||
| 46|SupportsHKPairingAndAccessControl||
| 47|||
| 48|SupportsTransientPairing|`SupportsSystemPairing` implies `SupportsTransientPairing`|
| 49|||
| 50|MetadataFeature4|bit 4 of MetadataFeatures. binary plist.|
| 51|SupportsUnifiedPairSetupAndMFi|Authentication type 8. MFi authentication|
| 52|SupportsSetPeersExtendedMessage||
