#!/usr/bin/env python3
"""Build a 1 MiB initial-production image: Boot + valid metadata + App-A."""

import argparse
import struct
import zlib
from pathlib import Path

FLASH_SIZE = 0x100000
BOOT_OFFSET = 0
META_A_OFFSET = 0x4000
APP_A_OFFSET = 0x5000
APP_A_CAPACITY = 0x7B000
META_MAGIC = 0x4D455441
SLOT_CONFIRMED = 1
SLOT_EMPTY = 0


def slot_info(size: int, crc: int, major: int, minor: int, build_time: int, state: int):
    return struct.pack("<IIHHIB3x", size, crc, major, minor, build_time, state)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--app-a", type=Path, required=True)
    parser.add_argument("--version", default="1.0")
    parser.add_argument("--build-time", type=int, default=0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    boot = args.boot.read_bytes()
    app = args.app_a.read_bytes()
    if len(boot) > 0x4000:
        raise ValueError("bootloader exceeds 16 KiB")
    if len(app) > APP_A_CAPACITY:
        raise ValueError("App-A exceeds its slot")
    major_text, minor_text = args.version.split(".")
    major, minor = int(major_text), int(minor_text)
    crc = zlib.crc32(app) & 0xFFFFFFFF
    prefix = struct.pack("<IHHI4B", META_MAGIC, 1, 60, 1, 0, 0xFF, 0, 0)
    body = prefix + slot_info(len(app), crc, major, minor, args.build_time, SLOT_CONFIRMED)
    body += slot_info(0, 0, 0, 0, 0, SLOT_EMPTY)
    if len(body) != 56:
        raise AssertionError(f"unexpected metadata body size {len(body)}")
    metadata = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    if len(metadata) != 60:
        raise AssertionError("metadata must be 60 bytes")

    image = bytearray(b"\xFF" * FLASH_SIZE)
    image[BOOT_OFFSET:BOOT_OFFSET + len(boot)] = boot
    image[META_A_OFFSET:META_A_OFFSET + len(metadata)] = metadata
    image[APP_A_OFFSET:APP_A_OFFSET + len(app)] = app
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"{args.output}: {len(image)} bytes, App-A v{major}.{minor}, "
          f"{len(app)} bytes, CRC32=0x{crc:08X}")


if __name__ == "__main__":
    main()
