#!/usr/bin/env python3
"""主机侧自测: 不需要硬件/ARM 工具链, 纯 Python 标准库即可运行。

覆盖:
  1. CRC16/CRC32 与 C 实现及 zlib 的一致性
  2. 帧打包/解析 (粘包、错误 CRC、超长、SOF 重同步)
  3. Flash 分区不重叠、页对齐、总容量
  4. .fw 打包与校验 (含向量表合法性检查、篡改检测)
  5. factory.bin 布局与元数据 CRC
  6. 三方常量一致性: GD32 C 头 / ESP32-S3 C 头 / Python 工具

运行: python -m unittest discover -s tests -v   (或 make test)
"""

import json
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
PY = sys.executable


def source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def macro_int(text: str, name: str) -> int:
    match = re.search(r"#define\s+" + re.escape(name) +
                      r"\s+(0x[0-9A-Fa-f]+|[0-9]+)", text)
    if match is None:
        raise AssertionError(f"macro {name} not found")
    return int(match.group(1), 0)


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                  else (crc << 1) & 0xFFFF
    return crc


def crc32_bitwise(data: bytes) -> int:
    """与 common/protocol/crc.c 的逐位实现完全一致"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def pack_frame(command: int, payload: bytes = b"") -> bytes:
    body = bytes([command]) + struct.pack("<H", len(payload)) + payload
    return b"\xA5\x5A" + body + struct.pack("<H", crc16_ccitt_false(body))


class FrameParser:
    """与 esp32_s3/main/ota_protocol.c 相同的状态机 (测试用 Python 版)"""

    def __init__(self):
        self.state = 0
        self.command = 0
        self.length = 0
        self.index = 0
        self.received_crc = 0
        self.payload = bytearray(300)
        self.frames = []

    def reset(self):
        self.__init__()

    def feed(self, data: bytes):
        for byte in data:
            s = self.state
            if s == 0:
                if byte == 0xA5:
                    self.state = 1
            elif s == 1:
                self.state = 2 if byte == 0x5A else 0
                if byte == 0xA5 and self.state == 0:
                    self.state = 1
            elif s == 2:
                self.command = byte
                self.state = 3
            elif s == 3:
                self.length = byte
                self.state = 4
            elif s == 4:
                self.length |= byte << 8
                self.index = 0
                if self.length > 300:
                    self.reset_data()
                else:
                    self.state = 5 if self.length else 6
            elif s == 5:
                self.payload[self.index] = byte
                self.index += 1
                if self.index >= self.length:
                    self.state = 6
            elif s == 6:
                self.received_crc = byte
                self.state = 7
            elif s == 7:
                self.received_crc |= byte << 8
                check = bytes([self.command, self.length & 0xFF,
                               self.length >> 8]) + bytes(self.payload[:self.length])
                if crc16_ccitt_false(check) == self.received_crc:
                    self.frames.append((self.command,
                                        bytes(self.payload[:self.length])))
                self.reset_data()

    def reset_data(self):
        self.state = 0
        self.command = 0
        self.length = 0
        self.index = 0
        self.received_crc = 0


class TestCrc(unittest.TestCase):
    def test_crc16_known_vector(self):
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_crc32_bitwise_matches_zlib(self):
        """GD32 用逐位 CRC32, PC 打包工具用 zlib.crc32, 二者必须完全相同"""
        for data in (b"", b"123456789", bytes(range(256)) * 4,
                     b"GD32F303 A/B OTA firmware image"):
            self.assertEqual(crc32_bitwise(data), zlib.crc32(data) & 0xFFFFFFFF)

    def test_crc32_known_vector(self):
        self.assertEqual(crc32_bitwise(b"123456789"), 0xCBF43926)

    def test_c_source_polynomials(self):
        crc_c = source("common/protocol/crc.c")
        self.assertIn("0x1021", crc_c)
        self.assertIn("0xEDB88320", crc_c)
        self.assertIn("0xFFFFFFFF", crc_c)


class TestProtocolFrames(unittest.TestCase):
    def test_round_trip_various_lengths(self):
        for length in (0, 1, 3, 40, 258, 300):
            payload = bytes((i * 7 + 3) & 0xFF for i in range(length))
            parser = FrameParser()
            parser.feed(pack_frame(0x11, payload))
            self.assertEqual(parser.frames, [(0x11, payload)],
                             f"length={length}")

    def test_bad_crc_rejected(self):
        frame = bytearray(pack_frame(0x10, b"abcd"))
        frame[-1] ^= 0xFF
        parser = FrameParser()
        parser.feed(bytes(frame))
        self.assertEqual(parser.frames, [])

    def test_overlength_payload_rejected_and_resync(self):
        parser = FrameParser()
        parser.feed(b"\xA5\x5A\x11\xFF\xFF" + b"\x00" * 4)
        parser.feed(pack_frame(0x01))
        self.assertEqual(parser.frames, [(0x01, b"")])

    def test_sticky_packets_and_garbage_prefix(self):
        parser = FrameParser()
        blob = b"\x00\xFF garbage \xA5" + pack_frame(0x10, b"H") + \
               pack_frame(0x12, b"\x01\x00\x00")
        parser.feed(blob)
        self.assertEqual(parser.frames,
                         [(0x10, b"H"), (0x12, b"\x01\x00\x00")])

    def test_crc_covers_command_length_and_payload(self):
        body = bytes([0x11]) + struct.pack("<H", 3) + b"abc"
        frame = pack_frame(0x11, b"abc")
        self.assertEqual(frame[2:2 + len(body)], body)
        self.assertEqual(struct.unpack_from("<H", frame, len(frame) - 2)[0],
                         crc16_ccitt_false(body))

    def test_frame_overhead(self):
        self.assertEqual(len(pack_frame(0x01)), 7)
        self.assertEqual(len(pack_frame(0x11, bytes(256))), 263)


class TestPartitionMap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = source("common/boot_shared/memory_map.h")

    def setUp(self):
        m = self.m
        self.base = macro_int(m, "FLASH_BASE_ADDR")
        self.total = macro_int(m, "FLASH_TOTAL_SIZE")
        self.boot_base = macro_int(m, "BOOT_BASE_ADDR")
        self.boot_size = macro_int(m, "BOOT_MAX_SIZE")
        self.meta_a = macro_int(m, "META_PAGE_A_ADDR")
        self.meta_b = macro_int(m, "META_PAGE_B_ADDR")
        self.meta_bytes = macro_int(m, "META_PAGE_BYTES")
        self.slot_a = macro_int(m, "SLOT_A_ADDR")
        self.slot_a_size = macro_int(m, "SLOT_A_SIZE")
        self.slot_b = macro_int(m, "SLOT_B_ADDR")
        self.slot_b_size = macro_int(m, "SLOT_B_SIZE")
        self.bank0_page = macro_int(m, "BANK0_PAGE_SIZE")
        self.bank1_page = macro_int(m, "BANK1_PAGE_SIZE")

    def test_capacity(self):
        self.assertEqual(self.total, 0x100000)
        self.assertEqual(self.base, 0x08000000)
        self.assertEqual(self.base + self.total, 0x08100000)

    def test_no_overlap_and_full_coverage(self):
        regions = [
            ("boot", self.boot_base, self.boot_size),
            ("meta_a", self.meta_a, self.meta_bytes),
            ("meta_b", self.meta_b, self.meta_bytes),
            ("slot_a", self.slot_a, self.slot_a_size),
            ("slot_b", self.slot_b, self.slot_b_size),
        ]
        for i, (name, addr, size) in enumerate(regions):
            self.assertGreaterEqual(addr, self.base, name)
            self.assertLessEqual(addr + size, self.base + self.total, name)
            for other_name, other_addr, other_size in regions[i + 1:]:
                overlap = addr < other_addr + other_size and \
                          other_addr < addr + size
                self.assertFalse(overlap, f"{name} overlaps {other_name}")
        self.assertEqual(self.boot_base + self.boot_size, self.meta_a)
        self.assertEqual(self.meta_b + self.meta_bytes, self.slot_a)
        self.assertEqual(self.slot_a + self.slot_a_size, self.slot_b)
        self.assertEqual(self.slot_b + self.slot_b_size,
                         self.base + self.total)

    def test_page_alignment(self):
        self.assertEqual(self.meta_a % self.bank0_page, 0)
        self.assertEqual(self.meta_b % self.bank0_page, 0)
        self.assertEqual(self.slot_a % self.bank0_page, 0)
        self.assertEqual(self.slot_b % self.bank1_page, 0)
        self.assertEqual(self.bank0_page, 0x800)
        self.assertEqual(self.bank1_page, 0x1000)
        self.assertEqual(self.slot_b, macro_int(self.m, "BANK0_END_ADDR"))

    def test_python_and_esp32_constants_match(self):
        fwpack = source("tools/fwpack.py")
        factory = source("tools/factory_image.py")
        self.assertIn(f'0x{self.slot_a:08X}', fwpack)
        self.assertIn(f'0x{self.slot_b:08X}', fwpack)
        self.assertIn(f'0x{self.slot_a_size:08X}', fwpack)
        self.assertIn(f'0x{self.slot_b_size:08X}', fwpack)
        self.assertIn("META_A_OFFSET = 0x4000", factory)
        self.assertIn("APP_A_OFFSET = 0x5000", factory)
        self.assertIn("APP_A_CAPACITY = 0x7B000", factory)
        self.assertIn("FLASH_SIZE = 0x100000", factory)


class TestThreeWayProtocolConstants(unittest.TestCase):
    def test_command_ids(self):
        gd = source("common/protocol/boot_protocol.h")
        esp = source("esp32_s3/main/ota_protocol.h")
        serial_tool = source("tools/serial_ota.py")
        names = ["CMD_HANDSHAKE", "CMD_HANDSHAKE_RESP",
                 "CMD_FIRMWARE_HEADER", "CMD_FIRMWARE_DATA",
                 "CMD_PACKET_ACK", "CMD_FIRMWARE_END",
                 "CMD_VERIFY_RESULT", "CMD_VERSION_QUERY"]
        for name in names:
            value = macro_int(gd, name)
            self.assertEqual(macro_int(esp, name), value, name)
            # Python 工具里的常量是十进制/十六进制字面量
            match = re.search(name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)",
                              serial_tool)
            self.assertIsNotNone(match, name)
            self.assertEqual(int(match.group(1), 0), value, name)

    def test_chunk_and_header_ack(self):
        gd = source("common/protocol/boot_protocol.h")
        esp = source("esp32_s3/main/ota_protocol.h")
        for name in ("PROTO_SOF0", "PROTO_SOF1", "PROTO_MAX_PAYLOAD",
                     "PROTO_DATA_CHUNK", "PROTO_HEADER_ACK_SEQ", "ACK_OK"):
            self.assertEqual(macro_int(esp, name), macro_int(gd, name), name)

    def test_firmware_header_size(self):
        header = source("common/protocol/firmware_header.h")
        self.assertEqual(macro_int(header, "FW_PACKAGE_HEADER_SIZE"), 40)
        self.assertEqual(struct.calcsize("<IHHIII20s"), 40)
        self.assertEqual(macro_int(header, "FW_MAGIC"), 0x4641574C)

    def test_metadata_struct_size(self):
        factory = source("tools/factory_image.py")
        self.assertIn("metadata must be 60 bytes", factory)
        # magic4 + fmt2 + len2 + seq4 + 4*uint8 + 2*slot_info(20) + crc4
        self.assertEqual(struct.calcsize("<IHHI4B40sI"), 60)


def fake_image(slot_base: int, size: int = 4096) -> bytes:
    """构造一个带合法向量表的假固件镜像"""
    image = bytearray(bytes((i * 31 + 7) & 0xFF for i in range(size)))
    msp = 0x20018000
    reset = (slot_base + 0x101) | 1  # Thumb 位必须置 1
    struct.pack_into("<II", image, 0, msp, reset)
    return bytes(image)


class TestFirmwarePackage(unittest.TestCase):
    def test_pack_verify_and_tamper(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            image = fake_image(0x08005000)
            image_path = tmp / "app_a.bin"
            package = tmp / "app_a.fw"
            manifest = tmp / "manifest.json"
            image_path.write_bytes(image)
            subprocess.run([PY, str(TOOLS / "fwpack.py"), "pack",
                            "--input", str(image_path),
                            "--output", str(package),
                            "--slot", "A", "--version", "1.2",
                            "--manifest", str(manifest)],
                           check=True, capture_output=True)
            raw = package.read_bytes()
            magic, major, minor, size, crc, build_time, reserved = \
                struct.unpack_from("<IHHIII20s", raw)
            self.assertEqual(magic, 0x4641574C)
            self.assertEqual((major, minor), (1, 2))
            self.assertEqual(size, len(image))
            self.assertEqual(crc, zlib.crc32(image) & 0xFFFFFFFF)
            self.assertEqual(reserved, bytes(20))
            self.assertEqual(raw[40:], image)
            data = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertEqual(data["version"], "1.2")
            self.assertEqual(data["packages"]["A"], "app_a.fw")

            result = subprocess.run([PY, str(TOOLS / "fwpack.py"), "verify",
                                     "--package", str(package), "--slot", "A"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"crc32"', result.stdout)

            tampered = bytearray(raw)
            tampered[40 + 100] ^= 0xFF
            (tmp / "bad.fw").write_bytes(bytes(tampered))
            result = subprocess.run([PY, str(TOOLS / "fwpack.py"), "verify",
                                     "--package", str(tmp / "bad.fw"),
                                     "--slot", "A"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_slot_b_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            image = fake_image(0x08080000)
            (tmp / "b.bin").write_bytes(image)
            result = subprocess.run([PY, str(TOOLS / "fwpack.py"), "pack",
                                     "--input", str(tmp / "b.bin"),
                                     "--output", str(tmp / "b.fw"),
                                     "--slot", "B", "--version", "2.0"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len((tmp / "b.fw").read_bytes()), len(image) + 40)

    def test_bad_vector_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            image = bytearray(fake_image(0x08005000, 256))
            struct.pack_into("<I", image, 0, 0x00000000)  # 非法 MSP
            (tmp / "bad.bin").write_bytes(bytes(image))
            result = subprocess.run([PY, str(TOOLS / "fwpack.py"), "pack",
                                     "--input", str(tmp / "bad.bin"),
                                     "--output", str(tmp / "x.fw"),
                                     "--slot", "A", "--version", "1.0"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)


class TestFactoryImage(unittest.TestCase):
    def test_layout_and_metadata_crc(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            boot = bytes((i * 13 + 5) & 0xFF for i in range(8192))
            app = fake_image(0x08005000, 20480)
            (tmp / "boot.bin").write_bytes(boot)
            (tmp / "app.bin").write_bytes(app)
            result = subprocess.run([PY, str(TOOLS / "factory_image.py"),
                                     "--boot", str(tmp / "boot.bin"),
                                     "--app-a", str(tmp / "app.bin"),
                                     "--version", "1.0",
                                     "--output", str(tmp / "factory.bin")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            blob = (tmp / "factory.bin").read_bytes()
            self.assertEqual(len(blob), 0x100000)
            self.assertEqual(blob[0:len(boot)], boot)
            self.assertEqual(blob[0x5000:0x5000 + len(app)], app)

            meta = blob[0x4000:0x4000 + 60]
            magic, fmt, length, seq, active, pending, request, trial = \
                struct.unpack_from("<IHHI4B", meta)
            self.assertEqual(magic, 0x4D455441)
            self.assertEqual((fmt, length, seq), (1, 60, 1))
            self.assertEqual((active, pending, request, trial),
                             (0, 0xFF, 0, 0))
            slot_a = struct.unpack_from("<IIHHIB3x", meta, 16)
            self.assertEqual(slot_a[0], len(app))
            self.assertEqual(slot_a[1], zlib.crc32(app) & 0xFFFFFFFF)
            self.assertEqual(slot_a[5], 1)  # SLOT_STATE_CONFIRMED
            self.assertEqual(struct.unpack_from("<I", meta, 56)[0],
                             zlib.crc32(meta[:56]) & 0xFFFFFFFF)
            # 未使用区域保持 0xFF
            self.assertEqual(blob[0x5000 + len(app):0x5000 + len(app) + 16],
                             b"\xFF" * 16)


if __name__ == "__main__":
    unittest.main(verbosity=2)
