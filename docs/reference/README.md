# AirPlay specification offline archive

- Website entry: [airplay-spec/index.html](airplay-spec/index.html)
- Markdown source: [airplay-spec-source/src/SUMMARY.md](airplay-spec-source/src/SUMMARY.md)
- Original site: https://openairplay.github.io/airplay-spec/
- Original repository: https://github.com/openairplay/airplay-spec
- Source revision: `00063da0e7f16c20fbfb5d29331334da1a89590a`

The archive contains every linked book chapter, the print edition, scripts,
stylesheets, fonts, search indexes, source Markdown, binary examples and source
assets. `manifest.json` records the download time and SHA-256 of every file.
All local HTML links were checked. External references remain external links;
they are not part of the AirPlay specification website.

Open `airplay-spec/index.html` directly, or serve locally (search also has a
file-URL fallback):

```sh
python3 -m http.server 8088 --bind 127.0.0.1 --directory docs/reference/airplay-spec
```

Refresh with `python3 scripts/mirror-airplay-spec.py`. It also downloads dynamic
search indexes and Google Fonts that a recursive wget alone misses. Two upstream
404s are recorded in the manifest: absent robots.txt and a malformed email link.
The latter is repaired to a mailto link in the offline copy. Query strings in
font filenames are normalized for Windows-compatible extraction. Protocol text
and examples are preserved unchanged.

This is third-party reference material, not original Switch TV Box documentation.
Original authorship and notices are retained. The upstream snapshot contains no
repository-wide LICENSE file; the application's GPL license does not relicense
this reference archive or its individually licensed assets.
