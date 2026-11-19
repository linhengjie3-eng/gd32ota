#!/usr/bin/env python3
"""Upload a .fw package through the GD32 ESP UART protocol (PC test tool)."""

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError:  # 允许在未安装 pyserial 时导入本模块做常量校验/测试
    serial = None

HEADER = struct.Struct("<IHHIII20s")
MAGIC = 0x4641574C
SOF = b"\xA5\x5A"
CMD_HANDSHAKE = 0x01
CMD_HANDSHAKE_RESP = 0x02
CMD_FIRMWARE_HEADER = 0x10
CMD_FIRMWARE_DATA = 0x11
CMD_PACKET_ACK = 0x12
CMD_FIRMWARE_END = 0x13
CMD_VERIFY_RESULT = 0x14
CMD_VERSION_QUERY = 0x20
CHUNK = 256

# 与 common/protocol/boot_protocol.h 同步；用于把设备的 NAK 翻译成可诊断信息
ACK_OK = 0
ACK_CRC_ERROR = 1
ACK_SEQUENCE_ERROR = 2
ACK_STATE_ERROR = 3
ACK_FLASH_ERROR = 4
ACK_BUSY = 5
ACK_LENGTH_ERROR = 6
ACK_VERSION_ERROR = 7
ACK_NAMES = {
    ACK_OK: "OK", ACK_CRC_ERROR: "CRC_ERROR",
    ACK_SEQUENCE_ERROR: "SEQUENCE_ERROR", ACK_STATE_ERROR: "STATE_ERROR",
    ACK_FLASH_ERROR: "FLASH_ERROR", ACK_BUSY: "BUSY",
    ACK_LENGTH_ERROR: "LENGTH_ERROR", ACK_VERSION_ERROR: "VERSION_ERROR",
}


def version_tuple(major: int, minor: int) -> tuple[int, int]:
    return (major, minor)


def version_is_downgrade(new: tuple[int, int], current: tuple[int, int]) -> bool:
    """与 GD32 Boot / ESP32 网关同语义：只有严格更低才算降级。"""
    return new < current


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(command: int, payload: bytes = b"") -> bytes:
    body = bytes([command]) + struct.pack("<H", len(payload)) + payload
    return SOF + body + struct.pack("<H", crc16(body))


class ProtocolError(RuntimeError):
    pass


class Uploader:
    def __init__(self, port: str, baudrate: int):
        self.serial = serial.Serial(port, baudrate, timeout=0.1)
        self.buffer = bytearray()

    def close(self):
        self.serial.close()

    def send(self, command: int, payload: bytes = b""):
        raw = frame(command, payload)
        self.serial.write(raw)
        self.serial.flush()

    def receive(self, timeout: float):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while True:
                start = self.buffer.find(SOF)
                if start < 0:
                    self.buffer.clear()
                    break
                if start:
                    del self.buffer[:start]
                if len(self.buffer) < 7:
                    break
                length = self.buffer[3] | self.buffer[4] << 8
                if length > 300:
                    del self.buffer[:2]
                    continue
                total = length + 7
                if len(self.buffer) < total:
                    break
                raw = bytes(self.buffer[:total])
                del self.buffer[:total]
                if crc16(raw[2:-2]) != struct.unpack_from("<H", raw, total - 2)[0]:
                    continue
                return raw[2], raw[5:-2]
            self.buffer.extend(self.serial.read(256))
        raise TimeoutError("timed out waiting for GD32 response")

    def wait_for(self, command: int, timeout: float):
        while True:
            got, payload = self.receive(timeout)
            if got == command:
                return payload

    def request(self, command: int, payload: bytes, response: int, timeout: float, retries=5):
        for attempt in range(retries):
            self.send(command, payload)
            try:
                return self.wait_for(response, timeout)
            except TimeoutError:
                if attempt + 1 == retries:
                    raise
                print(f"retry {attempt + 1}/{retries - 1} for command 0x{command:02X}")
        raise ProtocolError("unreachable")

    def send_packet(self, sequence: int, payload: bytes, timeout=2.0):
        for attempt in range(6):
            self.send(CMD_FIRMWARE_DATA, struct.pack("<H", sequence) + payload)
            try:
                ack = self.wait_for(CMD_PACKET_ACK, timeout)
            except TimeoutError:
                if attempt == 5:
                    raise
                continue
            ack_sequence, status = struct.unpack("<HB", ack)
            if ack_sequence != sequence:
                continue
            if status == 0:
                return
            if attempt == 5:
                raise ProtocolError(f"packet {sequence} NAK status {status}")
        raise ProtocolError(f"packet {sequence} failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB-UART port, e.g. COM6")
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--slot", choices=("A", "B"), required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--yes", action="store_true", help="confirm erasing the inactive slot")
    parser.add_argument("--force-bad-crc", action="store_true",
                        help="fault injection: corrupt the header CRC32 to test GD32-side rejection")
    parser.add_argument("--allow-downgrade", action="store_true",
                        help="allow flashing a strictly older version (needs Boot built with --allow-downgrade)")
    args = parser.parse_args()

    raw = bytearray(args.package.read_bytes())
    if len(raw) < HEADER.size + 8:
        raise ProtocolError("package too short")
    header = bytearray(raw[:HEADER.size])
    image = bytes(raw[HEADER.size:])
    if args.force_bad_crc:
        # crc32 字段位于头内偏移 12..15; 只改头, 镜像不动,
        # GD32 收完后 crc32_compute(flash) 与头里的值不符 -> VERIFY_CRC_FAILED
        header[12] ^= 0xFF
    magic, major, minor, size, expected_crc, build_time, _ = HEADER.unpack(header)
    if magic != MAGIC or size != len(image):
        raise ProtocolError("bad package header or size")
    if not args.force_bad_crc and \
            (zlib.crc32(image) & 0xFFFFFFFF) != expected_crc:
        raise ProtocolError("bad package CRC32")
    if not args.yes:
        raise SystemExit("dry run only; add --yes to erase and program the target slot")
    header = bytes(header)

    if serial is None:
        raise SystemExit("pyserial is required for serial I/O: "
                         "python -m pip install pyserial")

    uploader = Uploader(args.port, args.baud)
    try:
        response = uploader.request(CMD_VERSION_QUERY, b"",
                                    CMD_HANDSHAKE_RESP, 1.5)
        if len(response) != 12:
            raise ProtocolError("unexpected handshake payload")
        protocol, state, target = struct.unpack_from("<HBB", response)
        print(f"GD32 protocol=0x{protocol:04X} state={state} target={'AB'[target] if target < 2 else '?'}")
        if target != (0 if args.slot == "A" else 1):
            raise ProtocolError(f"GD32 requests slot {'AB'[target]}, not supplied slot {args.slot}")
        running_major, running_minor = struct.unpack_from("<HH", response, 4)
        print(f"GD32 running v{running_major}.{running_minor} -> package v{major}.{minor}")
        # 上位机预检：不发一个设备必然拒绝的低版本包，省掉整包传输 + 擦槽。
        # --force-bad-crc 是故意发坏包测设备侧 CRC 拒绝，跳过预检以免挡住测试。
        if (not args.force_bad_crc and not args.allow_downgrade and
                version_is_downgrade((major, minor),
                                     (running_major, running_minor))):
            raise ProtocolError(
                f"GD32 已运行 v{running_major}.{running_minor}，拒绝降级到 "
                f"v{major}.{minor}。要回最初版本用 SWD 出厂烧录: "
                f"python tools/build.py --version 1.0 flash-factory；"
                f"若确需 OTA 降级，需先用 --allow-downgrade 重编 Boot 并加本开关。")
        ack = uploader.request(CMD_FIRMWARE_HEADER, header, CMD_PACKET_ACK, 90.0)
        sequence, status = struct.unpack("<HB", ack)
        if sequence != 0xFFFF or status != ACK_OK:
            raise ProtocolError(
                f"header NAK sequence={sequence} status={status} "
                f"({ACK_NAMES.get(status, 'UNKNOWN')})")
        started = time.monotonic()
        for offset in range(0, len(image), CHUNK):
            sequence = offset // CHUNK
            uploader.send_packet(sequence, image[offset:offset + CHUNK])
            if sequence % 16 == 0 or offset + CHUNK >= len(image):
                print(f"\r{(offset + len(image[offset:offset + CHUNK])) * 100 // len(image):3d}%", end="", flush=True)
        print()
        result = uploader.request(CMD_FIRMWARE_END, b"", CMD_VERIFY_RESULT, 15.0)
        if result[0] != 0:
            raise ProtocolError(f"verification failed with status {result[0]}")
        elapsed = time.monotonic() - started
        print(f"upgrade accepted: v{major}.{minor}, {size} bytes in {elapsed:.1f}s; GD32 will reset")
    finally:
        uploader.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ProtocolError, TimeoutError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
