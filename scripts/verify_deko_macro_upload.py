#!/usr/bin/env python3
"""Compare the real GPU RAM and Eden macro maps produced by two MME headers."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def parse_header(path: Path) -> dict:
    text = path.read_text()
    exports = re.findall(r"\b(MmeMacro\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*,", text)
    if not exports:
        raise ValueError(f"{path}: missing exported macro methods")
    words = [
        int(word, 16)
        for word in re.findall(r"^\s*(0x[0-9a-fA-F]+)\s*,\s*$", text, re.MULTILINE)
    ]
    uploads: dict[int, list[int]] = {}
    ram: dict[int, int] = {}
    entries: list[int] = []
    cursor = 0
    while cursor < len(words):
        header = words[cursor]
        cursor += 1
        mode, count = header >> 29, (header >> 16) & 0x1FFF
        subchannel, method = (header >> 13) & 7, header & 0x1FFF
        if mode != 5 or subchannel != 0 or count < 2:
            raise ValueError(f"{path}: unexpected upload header {header:#x}")
        if cursor + count > len(words):
            raise ValueError(f"{path}: truncated upload packet")
        address, data = words[cursor], words[cursor + 1 : cursor + count]
        cursor += count
        if method == 0x45:
            if address in uploads:
                raise ValueError(f"{path}: duplicate upload start {address}")
            uploads[address] = data
            for offset, value in enumerate(data, address):
                if offset in ram and ram[offset] != value:
                    raise ValueError(f"{path}: conflicting instruction at {offset}")
                ram[offset] = value
        elif method == 0x47:
            if entries or address != 0:
                raise ValueError(f"{path}: unexpected entry-table rewrite")
            entries = data
        else:
            raise ValueError(f"{path}: unexpected method {method:#x}")
    if not ram or set(ram) != set(range(len(ram))):
        raise ValueError(f"{path}: instruction RAM has gaps")
    if len(entries) != len(exports) or len(set(entries)) != len(entries):
        raise ValueError(f"{path}: inconsistent exported entry table")
    if any(int(value, 16) != 0xE00 + 2 * index
           for index, (_name, value) in enumerate(exports)):
        raise ValueError(f"{path}: unexpected exported method numbering")
    return {
        "exports": exports,
        "entries": entries,
        "uploads": uploads,
        "instructions": [ram[index] for index in range(len(ram))],
        "setup_word_count": len(words),
    }


def verify(original_path: Path, compatibility_path: Path) -> dict:
    original, compatibility = map(parse_header, (original_path, compatibility_path))
    for field in ("exports", "entries", "instructions"):
        if original[field] != compatibility[field]:
            raise ValueError(f"compatibility upload changes {field}")
    instructions = original["instructions"]
    if original["uploads"] != {0: instructions}:
        raise ValueError("reference must contain one complete upload at address 0")
    if set(compatibility["uploads"]) != set(original["entries"]):
        raise ValueError("compatibility upload must explicitly cover every entry")
    for entry in original["entries"]:
        if compatibility["uploads"][entry] != instructions[entry:]:
            raise ValueError(f"entry {entry} does not contain its full original suffix")
    packed = struct.pack(f"<{len(instructions)}I", *instructions)
    return {
        "verified": True,
        "instruction_words": len(instructions),
        "instruction_ram_sha256": hashlib.sha256(packed).hexdigest(),
        "entry_addresses": original["entries"],
        "exported_macro_count": len(original["exports"]),
        "original_setup_words": original["setup_word_count"],
        "compatibility_setup_words": compatibility["setup_word_count"],
        "all_entry_suffixes_identical": True,
        "all_entry_lookups_direct": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("compatibility", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(verify(args.original, args.compatibility), indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    print(report, end="")


if __name__ == "__main__":
    main()
