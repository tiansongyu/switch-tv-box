#!/usr/bin/env python3
"""Check pinned SDK ao_hos object data, ABI and external symbols after rebuild."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ORIGINAL_SHA256 = "3453001ca2834b0f93d74fca7ffe6abcda307838057b3a16970d71ec1940b0b5"


class Elf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        assert self.data[:6] == b"\x7fELF\x02\x01", "expected ELF64 little-endian"
        assert struct.unpack_from("<H", self.data, 18)[0] == 183, "expected AArch64"
        offset = struct.unpack_from("<Q", self.data, 40)[0]
        entsize, count, names = struct.unpack_from("<HHH", self.data, 58)
        self.sections = [struct.unpack_from("<IIQQQQIIQQ", self.data,
                                           offset+i*entsize) for i in range(count)]
        self.names = [self.string(self.payload(names), s[0]) for s in self.sections]
        self.symbols = []
        for i, section in enumerate(self.sections):
            if section[1] == 2:
                strings = self.payload(section[6])
                for start in range(0, section[5], section[9]):
                    name, info, other, shndx, value, size = struct.unpack_from(
                        "<IBBHQQ", self.payload(i), start)
                    symbol = self.string(strings, name)
                    if not symbol and shndx and info & 15 == 3:
                        symbol = self.names[shndx]
                    self.symbols.append((symbol, info, other, shndx, value, size))

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(0, offset)].decode()

    def payload(self, index):
        s = self.sections[index]
        return self.data[s[4]:s[4]+s[5]]

    def data_sections(self):
        return {name: self.payload(i) for i, name in enumerate(self.names)
                if name.startswith((".rodata", ".data"))}

    def data_relocations(self):
        result = {}
        for i, name in enumerate(self.names):
            if not name.startswith(".rela.data"):
                continue
            entries = []
            for offset in range(0, self.sections[i][5], 24):
                location, info, addend = struct.unpack_from(
                    "<QQq", self.payload(i), offset)
                entries.append((location, info & 0xffffffff,
                                self.symbols[info >> 32][0], addend))
            result[name] = entries
        return result

    def externals(self):
        return sorted(s[0] for s in self.symbols if s[0] and s[3] == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original")
    parser.add_argument("rebuilt")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    original, rebuilt = Elf(args.original), Elf(args.rebuilt)
    assert hashlib.sha256(original.data).hexdigest() == ORIGINAL_SHA256, (
        "Unknown original SDK driver: review source/ABI before replacing it")
    assert original.data_sections() == rebuilt.data_sections(), (
        "ao_driver/priv/options/channel layouts differ from SDK object")
    assert original.data_relocations() == rebuilt.data_relocations(), (
        "Driver callbacks or option type relocations differ from SDK object")
    assert original.externals() == rebuilt.externals(), "external ABI symbols changed"
    report = {
        "original_object_sha256": ORIGINAL_SHA256,
        "rebuilt_object_sha256": hashlib.sha256(rebuilt.data).hexdigest(),
        "data_sections_identical": sorted(original.data_sections()),
        "data_relocations_identical": True,
        "external_symbols_identical": True,
        "source_abi_static_assertions": True,
    }
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
