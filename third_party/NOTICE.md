Switch TV Box upstream baseline
===============================

This application derives from NX-Cast by Ode1l and contributors.
Source: https://github.com/Ode1l/NX-Cast
Revision: 0efdba4281122cdd97de0c3c1a7ebde7485d6d15 (0.3.2)
License: GPL-3.0; the original license is retained at /LICENSE.
Switch TV Box changes are documented in git history and CHANGELOG.md.
The offline specification is third-party reference material with its original
notices; see docs/reference/README.md. It is not part of the linked program.

Third-party components
======================

Dear ImGui
----------

Source: https://github.com/ocornut/imgui
Version: v1.91.9b
License: MIT, see `third_party/imgui/LICENSE.txt`.

Remix Icon Cast mark
--------------------

Source: https://github.com/Remix-Design/RemixIcon/blob/master/icons/Device/cast-fill.svg
The home-screen cast mark is drawn to match this filled icon. The source SVG is
archived at `assets/icon/remix-cast-fill.svg`.
License: MIT, see `assets/licenses/LICENSE.RemixIcon.MIT.txt`.

Switch font asset
-----------------

`assets/fonts/switch_font.ttf` was imported from the local wiliwili reference
project. The font license text is kept in `assets/fonts/LICENSE.SourceFont.txt`.

NXMP ImGui deko3d shader reference
----------------------------------

The embedded ImGui deko3d shader binaries in
`source/player/render/imgui/imgui_shaders.inc` were generated from the local
NXMP reference project's `romfs/shaders/imgui_vsh.dksh` and
`romfs/shaders/imgui_fsh.dksh`.

libsodium
---------

Source: https://github.com/jedisct1/libsodium
Version: devkitPro `switch-libsodium` 1.0.20-1 package selected by the build image.
License: ISC, see `assets/licenses/LICENSE.libsodium.txt`.

NX-Cast links libsodium for the Ed25519 operations required by AirPlay device
identity and pairing. No libsodium source is vendored in this repository.

PlayFair compatibility backend
------------------------------

Source: https://github.com/FDH2/UxPlay
Commit: `3ca7526387e894d6848b84c209de361c3bedd1ec`
License: GPL-3.0 for PlayFair; LGPL-2.1-or-later for the isolated response-table
wrapper. See `third_party/playfair/LICENSE.md` and
`third_party/playfair/PROVENANCE.md`.

NX-Cast vendors only the PlayFair compatibility subset, not UxPlay's server,
pairing, media, rendering, or platform code. The upstream project describes
the legal status of this compatibility technique as unclear. Inclusion is for
open-source research/homebrew interoperability and does not imply Apple
authorization, MFi certification, DRM support, or legal suitability in every
jurisdiction.

AirPlay 1 / AirPort Express shared RSA interoperability key
----------------------------------------------------------

Source: https://github.com/mikebrady/shairport-sync/blob/cf6998daa1876c5a8f97128a960c8afc0e731841/common.c
Revision: `cf6998daa1876c5a8f97128a960c8afc0e731841` (retrieved 2026-10-20).
The `super_secret_key` PEM constant is reproduced in
`source/protocol/airplay/security/legacy_rsa_key.h`. It is the publicly documented
shared AirPort Express compatibility key, not a user credential or this
receiver's unique pairing key.

License: MIT. The upstream copyright and complete permission/warranty notice
are retained at the top of `legacy_rsa_key.h`. The RSA/base64 adapter, SDP parser,
RTSP handler changes and tests in this project were implemented separately.
