#!/usr/bin/env python3
"""Render the production ImGui home UI on a host CPU, without Switch SDK tools.

Requires a C++ compiler, a C compiler and Python Pillow. This reads draw functions
from imgui_overlay.cpp; it does not maintain a separate approximation of the UI.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
FUNCTIONS = ["draw_home_text", "text_width", "draw_sized_centered_text", "switch_key_width",
             "draw_switch_key", "draw_switch_action_hints", "draw_home_screen"]


def function(source: str, name: str) -> str:
    # Declarations end in ';', definitions reach '{'. The included functions have
    # no braces in string literals; balance nested C++ scopes after the signature.
    match = re.search(r"^(?:void|float) " + re.escape(name) + r"\([^;{]*\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError(f"Production draw function missing: {name}")
    depth = 1
    end = match.end()
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[match.start():end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "artifacts" / "ui")
    args = parser.parse_args()
    from PIL import Image
    args.output_dir.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "source/player/render/imgui/imgui_overlay.cpp").read_text()
    header = '''#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <imgui.h>
extern "C" {
#include "player/view.h"
#include "player/ui/home.h"
}
#define NXCAST_APP_VERSION "preview"
struct SwitchActionHint { const char *button; const char *label; };
'''
    body = header + "\n\n".join(function(source, name) for name in FUNCTIONS)
    body += (ROOT / "assets/ui/preview_renderer.cpp").read_text()
    with tempfile.TemporaryDirectory(prefix="switch-tv-box-ui-") as temporary:
        build = Path(temporary)
        (build / "preview.cpp").write_text(body)
        subprocess.run(["cc", "-std=c11", "-I", str(ROOT / "source"), "-c",
                        str(ROOT / "source/player/ui/home.c"), "-o", str(build / "home.o")], check=True)
        imgui = ROOT / "third_party/imgui"
        subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(imgui),
                        "-I", str(ROOT / "source"), str(build / "preview.cpp"), str(build / "home.o"),
                        *(str(imgui / name) for name in ["imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"]),
                        "-o", str(build / "preview")], check=True)
        for scenario in ["home", "playing", "music", "music-paused", "pairing", "offline", "english", "long-title"]:
            ppm = build / f"{scenario}.ppm"
            subprocess.run([str(build / "preview"), str(ROOT / "assets/fonts/switch_font.ttf"),
                            str(ppm), scenario], check=True)
            output = args.output_dir / f"{scenario}.png"
            with Image.open(ppm) as image:
                image.save(output)
            print(output)


if __name__ == "__main__":
    main()
