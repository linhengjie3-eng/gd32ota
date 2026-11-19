#!/usr/bin/env python3
"""Create or verify a 40-byte-header OTA package for GD32F303 A/B slots."""

import argparse
import json
import struct
import time
import zlib
from pathlib import Path

MAGIC = 0x4641574C
HEADER = struct.Struct("<IHHIII20s")
FLASH_BASE = 0x08000000
SLOTS = {"A": (0x08005000, 0x0007B000), "B": (0x08080000, 0x00080000)}
SRAM_BASE = 0x20000000
SRAM_END = SRAM_BASE + 0x18000


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_version(value: str):
    parts = value.split(".")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("version must be MAJOR.MINOR")
    major, minor = (int(part, 10) for part in parts)
    if not (0 <= major <= 65535 and 0 <= minor <= 65535):
        raise argparse.ArgumentTypeError("version fields must fit uint16")
    return major, minor


def check_vectors(image: bytes, slot: str) -> None:
    if len(image) < 8:
        raise ValueError("image is too small to contain a vector table")
    stack, reset = struct.unpack_from("<II", image)
    base, capacity = SLOTS[slot]
    if not SRAM_BASE <= stack <= SRAM_END or stack & 7:
        raise ValueError(f"invalid initial MSP 0x{stack:08X}")
    if not reset & 1 or not base <= (reset & ~1) < base + len(image):
        raise ValueError(f"Reset_Handler 0x{reset:08X} is outside slot {slot}")


def package(args) -> None:
    image = args.input.read_bytes()
    check_vectors(image, args.slot)
    _, capacity = SLOTS[args.slot]
    if len(image) > capacity:
        raise ValueError(f"image is {len(image)} bytes; slot {args.slot} holds {capacity}")
    major, minor = parse_version(args.version)
    build_time = int(time.time())
    header = HEADER.pack(MAGIC, major, minor, len(image), crc32(image), build_time, bytes(20))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + image)
    print(f"{args.output}: slot={args.slot} version={major}.{minor} payload={len(image)} "
          f"crc32=0x{crc32(image):08X} package={HEADER.size + len(image)}")
    if args.manifest:
        manifest_path = args.manifest
        data = {}
        if manifest_path.exists():
            data = json.loads(manifest_path.read_text(encoding="utf-8"))
        data.setdefault("format", 1)
        data["version"] = f"{major}.{minor}"
        data["build_time"] = build_time
        data.setdefault("packages", {})[args.slot] = args.output.name
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def verify(args) -> None:
    raw = args.package.read_bytes()
    if len(raw) < HEADER.size + 8:
        raise ValueError("package is too short")
    magic, major, minor, size, expected_crc, build_time, reserved = HEADER.unpack_from(raw)
    image = raw[HEADER.size:]
    if magic != MAGIC:
        raise ValueError(f"bad magic 0x{magic:08X}")
    if size != len(image):
        raise ValueError(f"header size {size} != payload size {len(image)}")
    actual_crc = crc32(image)
    if actual_crc != expected_crc:
        raise ValueError(f"CRC mismatch: expected 0x{expected_crc:08X}, got 0x{actual_crc:08X}")
    check_vectors(image, args.slot)
    result = {"slot": args.slot, "version": f"{major}.{minor}", "size": size,
              "crc32": f"0x{actual_crc:08X}", "build_time": build_time,
              "reserved_all_zero": reserved == bytes(20)}
    print(json.dumps(result, indent=2))


def write_manifest(args) -> None:
    """用目录中已存在的 vX.Y 包重建 manifest.json，用于演示切换版本。"""
    major, minor = parse_version(args.version)
    packages = {}
    for slot in ("A", "B"):
        path = args.dir / ("app_%s_v%s.fw" % (slot.lower(), args.version))
        if not path.exists():
            raise SystemExit("missing package: %s" % path)
        raw = path.read_bytes()
        _, h_major, h_minor, *_ = HEADER.unpack_from(raw)
        if (h_major, h_minor) != (major, minor):
            raise SystemExit("%s 头里是 v%d.%d，与 --version %s 不符"
                             % (path.name, h_major, h_minor, args.version))
        packages[slot] = path.name
    out = args.manifest or (args.dir / "manifest.json")
    data = {"format": 1, "version": args.version,
            "build_time": int(time.time()), "packages": packages}
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("%s: version=%s A=%s B=%s"
          % (out, args.version, packages["A"], packages["B"]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    pack = sub.add_parser("pack")
    pack.add_argument("--input", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    pack.add_argument("--slot", choices=SLOTS, required=True)
    pack.add_argument("--version", required=True)
    pack.add_argument("--manifest", type=Path)
    pack.set_defaults(function=package)
    check = sub.add_parser("verify")
    check.add_argument("--package", type=Path, required=True)
    check.add_argument("--slot", choices=SLOTS, required=True)
    check.set_defaults(function=verify)
    man = sub.add_parser("manifest")
    man.add_argument("--version", required=True)
    man.add_argument("--dir", type=Path, default=Path("build/packages"))
    man.add_argument("--manifest", type=Path,
                     help="输出路径，默认 <dir>/manifest.json")
    man.set_defaults(function=write_manifest)
    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
