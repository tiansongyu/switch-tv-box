# Screen Mirroring

Screen mirroring is achieved by transmitting an *H.264* encoded video
stream over a TCP connection. This stream is packetized with a 128-byte
header. *AAC-ELD* audio is sent using the AirTunes protocol. As for the
master clock, it is synchronized using *NTP*.

Moreover, as soon as a client starts a video playback, a standard
AirPlay connection is made to send the video URL, and mirroring is
stopped. This avoids decoding and re-encoding the video, which would
incur a quality loss.
