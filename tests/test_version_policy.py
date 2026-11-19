#!/usr/bin/env python3
"""版本策略回归测试（主机侧，纯 Python，不需要 ARM 工具链或硬件）。

覆盖用户诉求里最容易"看起来实现了、实际没生效"的四件事：
  1. 版本号真的编译进固件：--version 变化必须触发全量重编并改变二进制
  2. 三道版本门槛的语义与跨端常量一致（GD32 C / ESP32 C / Python）
  3. factory / packages / flash-factory 拒绝"版本与二进制不符"的产物
  4. version_status.py 能发现 manifest 与实际 .fw 头版本不一致

运行: python -m unittest discover -s tests -v
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
import tempfile
import unittest
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


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def run(*args: str, cwd: Path = ROOT):
    return subprocess.run([str(a) for a in args], cwd=str(cwd),
                          capture_output=True, text=True)


class TestVersionSemantics(unittest.TestCase):
    """版本比较：三端必须是同一套语义（严格更高才算升级）。"""

    def test_python_reference_semantics(self):
        def newer(new, cur):
            if new[0] != cur[0]:
                return new[0] > cur[0]
            return new[1] > cur[1]

        cases = [
            ((1, 1), (1, 0), True),    # minor 增
            ((2, 0), (1, 9), True),    # major 增，minor 回绕
            ((1, 1), (1, 1), False),   # 同版本不算升级
            ((1, 0), (1, 1), False),   # minor 降
            ((0, 9), (1, 0), False),   # major 降
            ((65535, 0), (0, 0), True),
        ]
        for new, cur, expected in cases:
            self.assertEqual(newer(new, cur), expected, (new, cur))

    def test_serial_tool_downgrade_semantics(self):
        mod = load_module("serial_ota_under_test", TOOLS / "serial_ota.py")
        self.assertTrue(mod.version_is_downgrade((1, 0), (1, 1)))
        self.assertTrue(mod.version_is_downgrade((0, 9), (1, 0)))
        self.assertFalse(mod.version_is_downgrade((1, 1), (1, 1)))
        self.assertFalse(mod.version_is_downgrade((1, 2), (1, 1)))
        self.assertFalse(mod.version_is_downgrade((2, 0), (1, 9)))

    def test_three_way_ack_version_error(self):
        gd = source("common/protocol/boot_protocol.h")
        esp = source("esp32_s3/main/ota_protocol.h")
        tool = source("tools/serial_ota.py")
        self.assertEqual(macro_int(gd, "ACK_VERSION_ERROR"), 7)
        self.assertEqual(macro_int(esp, "ACK_VERSION_ERROR"), 7)
        self.assertRegex(tool, r"ACK_VERSION_ERROR\s*=\s*7")
        # 所有 ACK 值在三端保持一致，且 0..7 无空缺/冲突
        names = ["ACK_OK", "ACK_CRC_ERROR", "ACK_SEQUENCE_ERROR",
                 "ACK_STATE_ERROR", "ACK_FLASH_ERROR", "ACK_BUSY",
                 "ACK_LENGTH_ERROR", "ACK_VERSION_ERROR"]
        self.assertEqual([macro_int(gd, n) for n in names], list(range(8)))
        for name in names:
            self.assertEqual(macro_int(esp, name), macro_int(gd, name), name)

    def test_boot_blocks_downgrade_before_erase(self):
        boot = source("boot/src/main.c")
        self.assertIn("version_is_downgrade", boot)
        self.assertIn("ACK_VERSION_ERROR", boot)
        # 关键不变量：拒绝分支必须出现在 bsp_flash_erase / mark_target_invalid 之前，
        # 否则槽已经被擦掉，"保留旧固件"的保证就失效了。
        reject = boot.find("DOWNGRADE BLOCKED")
        erase = boot.find("mark_target_invalid()", boot.find("static void handle_header"))
        self.assertGreater(reject, 0)
        self.assertGreater(erase, 0)
        self.assertLess(reject, erase)
        self.assertIn("BOOT_ALLOW_DOWNGRADE", boot)

    def test_gateway_uses_single_comparison_helper(self):
        gw = source("esp32_s3/main/ota_gateway.c")
        self.assertEqual(gw.count("version_is_newer("), 2,
                         "定义 1 次 + version_acceptable() 默认分支 1 次")
        self.assertEqual(gw.count("version_acceptable("), 4,
                         "定义 1 次 + 升级检查/推送/update_check 三处调用")
        self.assertIn("CONFIG_OTA_ALLOW_DOWNGRADE", gw)
        self.assertNotIn("latest_major > running_major", gw,
                         "仍有内联重复比较，应统一走 version_is_newer")
        # .fw 头必须与 manifest 一致，且通过升级/降级策略
        self.assertIn("disagrees with manifest", gw)
        self.assertIn("not acceptable vs running", gw)


class TestBuildVersionIntegrity(unittest.TestCase):
    """构建脚本：版本号必须真的进产物，且不允许版本错配。"""

    def test_version_defs_rejects_malformed_versions(self):
        build = load_module("build_under_test", TOOLS / "build.py")
        for bad in ("1", "1.2.3", "abc", "-1.0", "1.", "65536.0", "1.65536"):
            with self.subTest(version=bad), self.assertRaises(SystemExit):
                build.version_defs(bad)
        self.assertEqual(build.version_defs("1.2")[0],
                         ["-DAPP_VERSION_MAJOR=1", "-DAPP_VERSION_MINOR=2"])
        self.assertEqual(build.version_defs("2.0")[0],
                         ["-DAPP_VERSION_MAJOR=2", "-DAPP_VERSION_MINOR=0"])

    def test_compile_flags_fingerprint_includes_defs(self):
        """改了版本号宏必须清 .o 全量重编，否则屏幕永远停在旧版本。"""
        build = load_module("build_under_test", TOOLS / "build.py")
        src = source("tools/build.py")
        self.assertIn('" ".join(CFLAGS + ASFLAGS + defs)', src)
        self.assertIn("compile.flags", src)
        self.assertIn("for old in obj_dir.glob(\"*.o\")", src)
        a = build.version_defs("1.0")[0]
        b = build.version_defs("1.1")[0]
        id_a = hashlib.sha1(" ".join(build.CFLAGS + build.ASFLAGS + a)
                            .encode("utf-8")).hexdigest()
        id_b = hashlib.sha1(" ".join(build.CFLAGS + build.ASFLAGS + b)
                            .encode("utf-8")).hexdigest()
        self.assertNotEqual(id_a, id_b)

    def test_factory_and_packages_guard_version_mismatch(self):
        src = source("tools/build.py")
        self.assertIn("factory.bin 会把元数据写成", src)
        self.assertIn("请先执行: python tools/build.py --version %s all", src)
        self.assertIn("def write_image_meta", src)
        self.assertIn("def factory_version_matches", src)
        self.assertIn("def ensure_factory_for_flash", src)

    def test_flash_factory_honors_explicit_version(self):
        """图 6/7 演示脚本第 1 步：--version 1.0 flash-factory 必须重建。"""
        result = run(PY, "tools/build.py", "--version", "1.0",
                     "flash-factory", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("按 --version 1.0 重建出厂镜像", out)
        self.assertIn("-DAPP_VERSION_MINOR=0", out)
        self.assertIn("即将烧录出厂镜像 v1.0", out)
        self.assertNotIn("0xC251", out)  # 与硬件无关，纯命令展开

    def test_flash_factory_without_version_reuses_recorded(self):
        """不写 --version 时沿用出厂记录，避免误把 v1.1 重建回 v1.0。"""
        result = run(PY, "tools/build.py", "flash-factory", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("出厂镜像", result.stdout)

    def test_factory_image_writes_requested_version_into_metadata(self):
        """factory_image.py --version 必须把版本写进元数据槽 A。"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            boot = bytes([0xFF]) * 16
            image = bytearray(bytes((i * 7 + 3) & 0xFF for i in range(256)))
            struct.pack_into("<II", image, 0, 0x20018000,
                             (0x08005000 + 0x101) | 1)
            (tmp / "boot.bin").write_bytes(boot)
            (tmp / "app.bin").write_bytes(bytes(image))
            out = tmp / "factory.bin"
            result = run(PY, "tools/factory_image.py", "--boot", tmp / "boot.bin",
                         "--app-a", tmp / "app.bin", "--version", "3.7",
                         "--output", out)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = out.read_bytes()
            self.assertEqual(len(data), 0x100000)
            # 元数据槽 A 起始 0x4000 + prefix 16，布局 <IIHHIB3x
            size, crc, major, minor, _bt, state = struct.unpack_from(
                "<IIHHIB3x", data, 0x4000 + 16)
            self.assertEqual((major, minor), (3, 7))
            self.assertEqual(state, 1)
            self.assertEqual(size, len(image))


class TestVersionStatusTool(unittest.TestCase):
    def test_detects_manifest_package_version_mismatch(self):
        """把 manifest 改成 v9.9 而 .fw 头是 v1.1，工具必须报错。"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fake_build = tmp / "build"
            pkg = fake_build / "packages"
            pkg.mkdir(parents=True)
            header = struct.Struct("<IHHIII20s")
            for slot in ("a", "b"):
                raw = header.pack(0x4641574C, 1, 1, 8, 0, 0, bytes(20))
                (pkg / ("app_%s_v1.1.fw" % slot)).write_bytes(raw + bytes(8))
            (pkg / "manifest.json").write_text(json.dumps({
                "version": "9.9",
                "packages": {"A": "app_a_v1.1.fw", "B": "app_b_v1.1.fw"},
            }), encoding="utf-8")

            script = TOOLS / "version_status.py"
            code = script.read_text(encoding="utf-8").replace(
                'ROOT = Path(__file__).resolve().parent.parent',
                'ROOT = Path(%r)' % str(tmp))
            patched = tmp / "version_status.py"
            patched.write_text(code, encoding="utf-8")
            result = run(PY, patched, "--check")
            self.assertEqual(result.returncode, 1)
            self.assertIn("头里是 v1.1，manifest 写的是 v9.9", result.stderr)

    def test_consistent_state_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            pkg = tmp / "build" / "packages"
            pkg.mkdir(parents=True)
            header = struct.Struct("<IHHIII20s")
            for slot in ("a", "b"):
                raw = header.pack(0x4641574C, 2, 3, 8, 0, 0, bytes(20))
                (pkg / ("app_%s_v2.3.fw" % slot)).write_bytes(raw + bytes(8))
            (pkg / "manifest.json").write_text(json.dumps({
                "version": "2.3",
                "packages": {"A": "app_a_v2.3.fw", "B": "app_b_v2.3.fw"},
            }), encoding="utf-8")
            script = TOOLS / "version_status.py"
            code = script.read_text(encoding="utf-8").replace(
                'ROOT = Path(__file__).resolve().parent.parent',
                'ROOT = Path(%r)' % str(tmp))
            patched = tmp / "version_status.py"
            patched.write_text(code, encoding="utf-8")
            result = run(PY, patched, "--check")
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
