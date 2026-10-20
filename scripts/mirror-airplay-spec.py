#!/usr/bin/env python3
"""Archive the complete AirPlay book, its source, fonts and search indexes."""
import argparse
import concurrent.futures
import datetime
import hashlib
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "docs/reference"
SITE = REF / "airplay-spec"
SOURCE = REF / "airplay-spec-source"
URL = "https://openairplay.github.io/airplay-spec/"
REPO = "https://github.com/openairplay/airplay-spec.git"


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": "SwitchTVBox-Documentation/1.0"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    path.write_bytes(data)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--complete", action="store_true", help="complete an existing wget/source snapshot")
    args = parser.parse_args()
    REF.mkdir(parents=True, exist_ok=True)
    if not args.complete:
        subprocess.run([
            "wget", "--mirror", "--page-requisites", "--convert-links",
            "--adjust-extension", "--no-parent", "--no-host-directories",
            "--cut-dirs=1", "--directory-prefix=" + str(SITE),
            "--output-file=" + str(REF / "mirror.log"), URL,
        ], check=False)  # Upstream has a broken author-email link; audited below.
        with tempfile.TemporaryDirectory(prefix="tvbox-spec-") as temporary:
            subprocess.run(["git", "clone", "--depth", "1", REPO, temporary], check=True)
            commit = subprocess.check_output(["git", "-C", temporary, "rev-parse", "HEAD"], text=True).strip()
            shutil.copytree(temporary, SOURCE, dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git"))
            (REF / "source-commit.txt").write_text(commit + "\n")

    if (SOURCE / ".git").exists():
        commit = subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True).strip()
        (REF / "source-commit.txt").write_text(commit + "\n")
        # Keep the clone metadata locally, outside the vendored tree.
        metadata = ROOT / "build/reference-source.git"
        metadata.parent.mkdir(parents=True, exist_ok=True)
        if metadata.exists():
            raise RuntimeError("build/reference-source.git already exists; inspect before replacing")
        shutil.move(str(SOURCE / ".git"), str(metadata))

    for name in ("searchindex.js", "searchindex.json"):
        download(URL + name, SITE / name)

    google_urls = set()
    for page in SITE.rglob("*.html"):
        google_urls.update(html.unescape(u) for u in re.findall(
            r'https://fonts\.googleapis\.com/[^"]+', page.read_text()))
    css_replacements = {}
    font_jobs = {}
    for index, url in enumerate(sorted(google_urls)):
        css_path = SITE / "fonts" / f"google-{index}.css"
        css = download(url, css_path).decode()
        for font_url in re.findall(r"url\((https://[^)]+)\)", css):
            suffix = Path(urllib.parse.urlsplit(font_url).path).suffix
            filename = hashlib.sha256(font_url.encode()).hexdigest()[:20] + suffix
            font_jobs[font_url] = css_path.parent / filename
            css = css.replace(font_url, filename)
        css_path.write_text(css)
        css_replacements[url] = css_path
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(lambda item: download(*item), font_jobs.items()))
    # Query marks in filenames break Windows extraction and some local servers.
    font_dir = SITE / "FontAwesome/fonts"
    for path in sorted(font_dir.glob("*?*")):
        target = path.with_name(path.name.split("?")[0])
        if not target.exists():
            path.rename(target)
        elif path.read_bytes() == target.read_bytes():
            path.unlink()
    for path in SITE.rglob("*.css"):
        css = re.sub(r"fontawesome-webfont(\.[a-z0-9]+)%3F(?:v=4\.7\.0)?",
                     r"fontawesome-webfont\1", path.read_text())
        path.write_text(css)
    for page in SITE.rglob("*.html"):
        content = page.read_text()
        for url, css_path in css_replacements.items():
            content = content.replace(url, os.path.relpath(css_path, page.parent))
            content = content.replace(html.escape(url, quote=True), os.path.relpath(css_path, page.parent))
        content = re.sub(r'href="(?:https://openairplay.github.io/airplay-spec/)?brian@maven-group.org"',
                         'href="mailto:brian@maven-group.org"', content)
        page.write_text(content)

    missing = set()

    class Links(HTMLParser):
        def handle_starttag(self, tag, attrs):
            for key, value in attrs:
                if key not in ("src", "href") or not value:
                    continue
                url = urllib.parse.urlsplit(value)
                if url.scheme or url.netloc or not url.path:
                    continue
                path = page.parent / urllib.parse.unquote(url.path)
                if path.is_dir():
                    path /= "index.html"
                if not path.exists():
                    missing.add(f"{page.relative_to(SITE)}: {value}")

    for page in SITE.rglob("*.html"):
        Links().feed(page.read_text())
    if missing:
        raise RuntimeError("Missing local links:\n" + "\n".join(sorted(missing)))
    files = {}
    for directory in (SITE, SOURCE):
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                files[str(path.relative_to(REF))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        "website": URL, "source_repository": REPO,
        "source_commit": (REF / "source-commit.txt").read_text().strip(),
        "downloaded_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "local_html_pages": len(list(SITE.rglob("*.html"))),
        "missing_local_links": [], "sha256": files,
        "offline_changes": ["local navigation, fonts and search indexes",
                            "normalize font filenames", "repair upstream author mailto link"],
        "upstream_unavailable": ["https://openairplay.github.io/robots.txt",
                                 URL + "brian@maven-group.org"],
    }
    (REF / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Archived {len(files)} files, {manifest['local_html_pages']} HTML pages; all local links resolve.")


if __name__ == "__main__":
    main()
