Switch TV Box AirPlay storage
=======================

Switch TV Box creates its device identity and trusted-client records in this
directory on first successful AirPlay startup.

Runtime files such as identity.bin and pairings.bin are private to this SD
card. Do not publish them, attach them to bug reports, or copy them into a
release package. Deleting them resets AirPlay identity and trusted pairings.

The receiver supports classic AirPlay 1 UDP audio (ALAC, AAC, PCM), JPEG/PNG
photos, URL/HLS playback and paired screen mirroring. Connect the sender and
Switch to the same local network. The build includes an auditable GPL
PlayFair compatibility backend. Physical-device compatibility still needs
validation; see docs/AIRPLAY1.md in the source repository for exact limits.
Commercial FairPlay/DRM content, Apple certification, AWDL, and AirPlay 2
multi-room audio are not supported.
