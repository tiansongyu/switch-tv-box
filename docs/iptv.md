# IPTV Support

NX-Cast keeps IPTV source, playlist, cache, search, favorite, and EPG handling in C. The C++ ImGui/deko3d frontend only reads snapshots and draws the browser. Playback continues to use the same libmpv/FFmpeg session as DLNA.

## Current Flow

```text
local M3U on SD or remote M3U URL
  -> classify channel list versus direct HLS playlist
  -> download remote files to an atomic SD cache
  -> discover and download url-tvg / x-tvg-url programme guides on the first refresh
  -> parse EXTINF metadata and resolve relative URLs
  -> apply favorites, recent history, group, and search filters
  -> apply cached XMLTV current/next programme data by tvg-id
  -> select a channel
  -> existing libmpv/FFmpeg/deko3d playback path
```

Remote refresh and logo downloads run on one background worker. Rendering and controller state remain on the main thread.

## Supported

- Local and remote `.m3u` / `.m3u8` sources
- Persistent remote source add, remove, refresh, and XMLTV URL configuration
- SD-card preinstalled remote sources through a user-editable `sources.txt`
- Atomic playlist, XMLTV, and channel-logo caches on the SD card
- `#EXTINF`, `tvg-id`, `tvg-name`, `tvg-logo`, `group-title`, `url-tvg`, and `x-tvg-url`
- Relative local URLs, relative remote URLs, root-relative URLs, and protocol-relative URLs
- All, Favorites, Recent, and M3U group filters
- Channel search by name, group, or `tvg-id`
- Persistent favorites and recent playback history
- Persistent playback quality and buffering preferences, accessible while watching
- Plain or gzip XMLTV parsing with current and next programme display
- HLS media/master playlist detection through `#EXT-X-*` tags
- Direct `http`, `https`, `rtsp`, `rtmp`, `udp`, `rtp`, `mms`, `file`, and `sdmc` playback URLs

`tvg-logo` files are cached asynchronously and displayed through a small deko3d texture cache. Channel names and programme metadata remain in the source's original language.

## Token URLs And Authentication Limits

HTTP/HTTPS URLs may contain query parameters such as tokens or signatures.
NX-Cast preserves the supplied URL when downloading a remote playlist or
opening an absolute channel URL, for example:

```text
https://example.com/channels.m3u?token=YOUR_TOKEN
https://example.com/live/index.m3u8?token=YOUR_TOKEN&sign=YOUR_SIGNATURE
```

These can be entered in the UI or stored in `sources.txt` using the format
below. This is URL handling, not a login or subscription integration; playback
still depends on the provider accepting that URL from the Switch.

- Custom `Authorization: Bearer ...` and Cookie headers cannot currently be configured. M3U header directives such as `#EXTVLCOPT` and `#KODIPROP` are not interpreted for authentication.
- There is no automatic login, token renewal or expired-link recovery. Replace an expired source URL and refresh its playlist as needed.
- A source playlist's query token is not automatically appended to its channel URLs. Each channel must resolve to an independently accessible URL; do not assume playlist authentication also authenticates media requests.
- IPTV URL storage allows up to 1,023 bytes, excluding the terminating null byte. Longer signed URLs are not supported.
- Source URLs and cached playlists are stored as plain text on the SD card, not in an encrypted credential store. Treat them as private when they contain tokens, and redact credentials from logs and reports. Never include personal tokens in Git or release presets.

Advanced authentication is not implemented; no additional authentication
support is being added as part of this documentation update.

## Controls

The full-screen library shows eight rows; the playback drawer shows nine.
There are no page buttons. Hold a direction to scroll faster, or use Search,
Categories, Favorites, Recent and the independent source filter to narrow a list.

- Home: A opens the focused Live TV card; X or a stick click also opens the library.
- Home: up/down focuses language/Live TV; A or touch switches Chinese/English.
- Library: up/down or either stick moves selection. Left/right or L/R moves
  between the toolbar, list and action bar. A/SR activates the focused item.
- Touch: drag the list or scrollbar, tap a row to select, then tap Play.
  A scrolling gesture never triggers playback.
- Y or the Favorite action toggles the selected channel's favorite status.
- Search accepts an empty query to clear the search.
- Sources opens a selector with All sources and Manage sources.
- Manage sources: select a source, then choose Add URL, Scan SD, Refresh,
  Programme guide, or Delete. Delete requires confirmation.
- B/SL dismisses selectors, returns from source management, or closes the browser.
- Playback settings opens quality/buffering preferences. Left/right or A changes
  the selected value; B cancels unsaved edits. Save applies the preferences and
  reloads the currently playing IPTV channel, without changing favorites.
- During IPTV playback X or a stick click opens a dark left drawer. X or the
  Full list button expands it without stopping playback. B collapses the full
  list to the drawer, then closes the drawer. Switching channels preserves
  the browser so another channel can be selected.
- Menu input is isolated from underlying playback pause, seek and volume actions.

Handheld controls, either stick, paired Joy-Cons, Pro Controller, a single
Joy-Con (SR confirm / SL back), and touch can navigate independently.
Local sources are removed by deleting their M3U file from the SD card.

## Playback Quality And Buffering

Open **播放设置 / Playback settings** from the channel toolbar, including the
drawer shown by X during playback. The preferences are saved in
`switch/switch-tv-box/iptv/playback-settings.conf`.

| Setting | Choices | Effect |
|---|---|---|
| Quality | Original / Balanced / Smooth | Select the highest advertised HLS bitrate / highest at or below 1.5 Mbps when available / lowest advertised bitrate |
| Buffering | Quick / Stable / Extra buffer | Target 6 / 30 / 60 seconds before starting or resuming after a buffer underrun |

The default is Original quality and Extra buffer; existing saved preferences
are preserved. Forward packet memory is bounded to 16 / 48 / 96 MiB
respectively, with read-ahead targets of 30 / 120 / 180 seconds. Including
backward cache, the largest mode uses at most 100 MiB of packet cache.
Initial buffering is enabled for
IPTV; more buffering trades startup time and live delay for tolerance of network
jitter. The actual buffered duration can be shorter at stream end or when the
memory limit is reached.

Quality selects a variant supplied by the source; it does not transcode video.
A single-variant 720p stream remains 720p in Smooth mode. A provider must supply
a lower-resolution or lower-bitrate variant, or a separate channel URL, to
reduce the bytes downloaded and the decoder workload. A sustained connection
slower than the stream bitrate will still stall even with a larger buffer.
These semantics follow the pinned
[mpv 0.36 HLS and cache options](https://github.com/mpv-player/mpv/blob/v0.36.0/DOCS/man/options.rst).

Changes apply to the current IPTV channel when saved, or to the next IPTV
playback when no IPTV session is active. AirPlay and DLNA keep their own
per-file settings. Network timeouts are 20 / 45 / 60 seconds respectively;
underlying network failures remain available in the runtime log.

## CCTV-3 Source Check (2026-10-20)

The installed **CCTV-3 综艺** entry at
`ottrrs.hl.chinamobile.com/PLTV/88888888/224/3221226021/index.m3u8`
failed before decoding: the configured proxy returned HTTP 502, and connecting
through the physical interface to its independently resolved IP timed out.
This establishes reachability failure on the test network, not permanent
unavailability of the broadcaster.

The existing **CCTV-3 (720p)** entry in the Chinese `iptv-org` list did provide
video in Eden. Its entry URL currently redirects to a single-variant HLS media
playlist; the sample was H.264 1280×720 with stereo AAC. The current playlist
advertised 10-second segments, with one segment around 4.14 MB (about 3.3 Mbps).
It does not offer a 360p/480p variant for the quality selector. Keep the original
channel entry URL: redirect URLs contain expiring tokens.

The stream has variable bitrate. In a later bounded check, a 10-second,
1.74 MB segment took 12.73 seconds through the host's Meta TUN path, while a
different 10-second, 2.24 MB segment took 6.36 seconds through `enp7s0`.
These are individual measurements, not a guarantee of continued bandwidth.
The former sample could not supply data as fast as playback consumed it.

## Capacity

Channels, sources, groups and favorites now grow on demand instead of using
fixed 4,096-channel, 32-source and 64-group arrays. The channel storage budget
is 128 MiB per catalog; metadata arrays each have an 8 MiB budget. A rebuild
temporarily holds both the old and new catalog. Allocation or budget failures
are reported and leave the existing catalog usable; this is not infinite storage.

Recent history retains the last 32 channels. Download-size safety limits and
Switch memory still apply. Large libraries should use source/category filters
and search rather than traversing the entire list. Rendering reads only the
visible rows; filter counts are cached when the view is rebuilt.

## SD Card Data

```text
sdmc:/switch/switch-tv-box/iptv/
  *.m3u / *.m3u8       local sources
  sources.txt           user-editable preinstalled HTTP/HTTPS sources
  sources.example.txt   packaged format example
  sources.tsv           generated remote source database; do not edit
  favorites.txt        stable channel IDs
  recent.txt           recent channel IDs
  cache/playlists/     downloaded remote M3U files
  cache/epg/           downloaded plain/gzip XMLTV files
  cache/logos/         downloaded tvg-logo files
```

The app owns the generated database and cache files. Manual playlists can still be copied directly into the root IPTV directory.

For long remote addresses, create `sources.txt` beside `sources.example.txt` and use either form:

```text
https://example.com/channels.m3u
My IPTV | https://example.com/channels.m3u | https://example.com/guide.xml
```

Blank lines and `#` comments are ignored. Fields must use the ASCII `|` separator. NX-Cast merges entries into its internal source database, deduplicates by playlist URL, and automatically refreshes new entries or entries whose EPG URL changed. Removing a remote source from the Sources screen removes its matching line from `sources.txt`, updates `sources.tsv`, and deletes its cached M3U and EPG files. If `sources.txt` is copied while NX-Cast is running, press `Y` on Home to reload it before refreshing.

Nxlink uploads only `NX-Cast.nro`; it does not synchronize the local `assets/` or `sdmc/` directories. For device testing, copy `sources.txt` to the physical Switch SD path shown above or install the complete `NX-Cast-sdmc.zip` package.

The standard release includes the public presets from `assets/iptv/sources.txt`. Release packaging fails if that file is missing, empty, or is not copied intact into `switch-tv-box-sdmc.zip`. Personal URLs containing credentials or tokens should remain only on the physical SD card and must not be committed as distributor presets.

## Remaining Scope

NXMP remains broader: network file systems, Enigma2, full-day EPG grids,
playlist editing, recording and timeshift are not implemented here.
The current guide displays the current/next programme when matching XMLTV
data is available; it cannot infer a channel's programme from video alone.

NX-Cast does not provide subscription credentials, DRM bypass or regional
access. Users must supply authorized playlists and streams.
