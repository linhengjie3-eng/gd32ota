#!/usr/bin/env python3
"""版本演示助手：离线检查 build/ 里各产物的版本号是否自洽，并打印演示脚本。

这是纯主机侧的只读工具：不连串口、不烧录、不改任何产物。
用途：在按 "v1.0 -> v1.1 -> v1.2 -> 回到 v1.0" 演示前，
先确认「屏幕显示的版本 / .fw 头版本 / manifest 版本 / 出厂元数据版本」
四者是同一个版本，避免把"升级完屏幕还是 v1.0"误判成 Boot 的 bug。

用法：
    python tools/version_status.py            # 打印状态表 + 演示脚本
    python tools/version_status.py --check    # 只做一致性检查，不一致时退出码 1
    python tools/build.py version-status      # 等价入口
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
PACKAGES = BUILD / "packages"
HEADER = struct.Struct("<IHHIII20s")
MAGIC = 0x4641574C


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def package_version(path: Path) -> tuple[str, int]:
    """返回 (版本字符串, 镜像字节数)；失败返回 ('?', 0)。"""
    try:
        raw = path.read_bytes()
    except OSError:
        return "?", 0
    if len(raw) < HEADER.size + 8:
        return "?", 0
    magic, major, minor, size, _crc, _time, _res = HEADER.unpack_from(raw)
    if magic != MAGIC or size != len(raw) - HEADER.size:
        return "?", 0
    return "%u.%u" % (major, minor), size


def gather() -> dict:
    factory = read_json(BUILD / "factory.meta.json")
    app_a = read_json(BUILD / "app_a.meta.json")
    app_b = read_json(BUILD / "app_b.meta.json")
    manifest = read_json(PACKAGES / "manifest.json")
    fw = {}
    for slot in ("a", "b"):
        for path in sorted(PACKAGES.glob("app_%s_v*.fw" % slot)):
            ver, _size = package_version(path)
            fw[path.name] = ver
    return {
        "factory": factory.get("version"),
        "app_a": app_a.get("version"),
        "app_b": app_b.get("version"),
        "manifest": manifest.get("version"),
        "manifest_packages": manifest.get("packages", {}),
        "fw": fw,
        "has_factory_bin": (BUILD / "factory.bin").is_file(),
        "has_512k": (BUILD / "factory_512k.bin").is_file(),
    }


def consistency_issues(info: dict) -> list[str]:
    issues: list[str] = []
    declared = info["manifest"]
    if declared is None:
        return ["找不到 build/packages/manifest.json，请先运行 "
                "python tools/build.py all"]
    for slot in ("A", "B"):
        name = info["manifest_packages"].get(slot)
        if not name:
            issues.append("manifest 缺少 packages.%s" % slot)
            continue
        ver = info["fw"].get(name)
        if ver is None:
            issues.append("manifest 指向的 %s 不存在" % name)
        elif ver != declared:
            issues.append("%s 头里是 v%s，manifest 写的是 v%s" %
                          (name, ver, declared))
    for key in ("app_a", "app_b"):
        if info[key] is not None and info[key] != declared:
            issues.append("build/%s.bin 是 v%s，与 manifest v%s 不一致" %
                          (key, info[key], declared))
    if info["factory"] is not None and info["factory"] != declared:
        issues.append("出厂镜像是 v%s，manifest 是 v%s（演示前建议先 flash-factory）"
                      % (info["factory"], declared))
    return issues


DEMO = """\
版本演示脚本（A/B 槽会交替，版本号必须严格递增）

  0. 先看本工具的状态表，确认四项版本一致。

  1. 回到最初版本（SWD 整片烧录，不可逆，会覆盖板内旧程序）：
       python tools/build.py --version 1.0 flash-factory
     预期：屏幕 "Application v1.0  Slot A"

  2. 发布 v1.1 并升级：
       python tools/build.py --version 1.1 all
       起固件服务器：python tools/firmware_server.py
       在 App 界面点 "Firmware update" -> "Enter upgrade mode"
     预期：屏幕 "Application v1.1  Slot B"，Boot 停在升级界面时目标槽为 B

  3. 发布 v1.2 再升级：
       python tools/build.py --version 1.2 all
       重复第 2 步的升级操作
     预期：屏幕 "Application v1.2  Slot A"

  4. 想重来 -> 回到第 1 步。

规则说明：
  * 每次 --version X.Y all 会同时重编 A/B 两个槽，两个 .fw 都标 X.Y；
    manifest 也写 X.Y。设备升级时 Boot 通过握手告诉上位机目标槽，
    上位机必须发对应槽的包（A 包不能发到 B，链接地址不同）。
  * 只有 X.Y 严格高于设备当前版本才会推送：
      - ESP32 网关用 version_is_newer() 判断，相等/更低直接打印
        "already up to date, nothing to do"；
      - PC 串口工具在发固件头前做同样的预检；
      - GD32 Boot 在擦槽前再拒绝一次严格降级（防回滚攻击）。
  * 同版本重刷是允许的（故障注入、A/B 压测需要），不会被当成升级。
  * "回到最初版本"用 SWD 出厂烧录，不是 OTA 降级：出厂烧录会重写
    元数据，把 active_slot 复位到 A、版本复位到 X.Y。

本工具为离线主机侧检查，不替代上板验证。
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="只做一致性检查，不一致时返回退出码 1")
    args = ap.parse_args()

    info = gather()
    issues = consistency_issues(info)

    if not args.check:
        print("=== 当前产物版本状态 ===")
        print("  出厂镜像 (factory.meta.json) : v%s%s" %
              (info["factory"] or "?", "" if info["has_factory_bin"] else "  [factory.bin 缺失]"))
        print("  App-A 编译版本               : v%s" % (info["app_a"] or "?"))
        print("  App-B 编译版本               : v%s" % (info["app_b"] or "?"))
        print("  服务器 manifest 版本         : v%s" % (info["manifest"] or "?"))
        if info["fw"]:
            for name, ver in sorted(info["fw"].items()):
                print("    %-28s 头版本 v%s" % (name, ver))
        print("  SWD 切片 factory_512k.bin    : %s" %
              ("存在" if info["has_512k"] else "尚未生成（烧录时自动生成）"))
        print("")
        if issues:
            print("=== 一致性问题 (%d) ===" % len(issues))
            for line in issues:
                print("  ! " + line)
        else:
            print("=== 一致性检查：OK（四项版本自洽）===")
        print("")
        print(DEMO)

    if issues:
        for line in issues:
            print("ERROR: " + line, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
