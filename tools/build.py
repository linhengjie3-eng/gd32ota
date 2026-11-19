#!/usr/bin/env python3
"""GD32F303VGT6 A/B OTA —— Windows 免 make 构建入口。

这是顶层 Makefile 的等价实现，用于不想安装 GNU Make / 不想折腾 Git Bash 的环境。
只依赖 Python 3 标准库；编译固件时才需要 Arm GNU Toolchain 在 PATH 里。

用法（在项目根目录或任意目录均可，脚本会自动定位根目录）：
    python tools/build.py check                 检查 arm-none-eabi-gcc 是否可用
    python tools/build.py test                  22 项主机自测（不需要任何工具链）
    python tools/build.py all                   编出全部镜像与 OTA 包
    python tools/build.py --version 1.1 all     指定 App 版本号
    python tools/build.py boot | app_a | app_b | factory | packages | clean
    python tools/build.py flash-factory | flash-boot | flash-app-a
    python tools/build.py help

可选参数：
    --gcc-path DIR        工具链 bin 目录（不在 PATH 里时指定）
    --openocd-path DIR    OpenOCD 的 bin 目录（默认自动探测 STM32CubeIDE 自带版）
    --openocd-cfg CFG     OpenOCD 目标配置，默认 config/openocd_gd32f303.cfg
    --openocd-iface CFG   覆盖接口配置（高级用法，默认已由 openocd-cfg 内含）
    --openocd-target CFG  覆盖目标配置（高级用法）
    --dry-run             只打印将执行的命令，不真正执行（用于排查构建参数）

Makefile 仍然是 GCC 路线的标准入口；本脚本与它参数完全一致，两者可混用
（但不要同时增量编译同一个 build/ 目录，改头文件后建议先 clean）。
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
FACTORY_META = BUILD / "factory.meta.json"
PREFIX = "arm-none-eabi-"

# ------------------------------------------------------------------ 依赖路径
CMSIS = "third_party/gd32f30x/Firmware/CMSIS"
GDINC = CMSIS + "/GD/GD32F30x/Include"
GDSRC = CMSIS + "/GD/GD32F30x/Source"
PERIPH = "third_party/gd32f30x/Firmware/GD32F30x_standard_peripheral"
RTOS = "third_party/FreeRTOS-Kernel"
LVGL = "third_party/lvgl"
STARTUP = GDSRC + "/GCC/startup_gd32f30x_xd.S"

# --------------------------------------------------------------- 编译/链接参数
# GD32F303VGT6 = Cortex-M4F (单精度 FPU)。FreeRTOS 的 ARM_CM4F port.c 会
# 保存/恢复 s16-s31, 必须带 FPU 参数, 否则汇编器报 "selected FPU does not support"。
MCU = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
DEFS = ["-DUSE_STDPERIPH_DRIVER", "-DGD32F30X_XD", "-DHXTAL_VALUE=8000000"]
OPT = ["-Os", "-g", "-gdwarf-2"]
WARN = ["-Wall", "-Wextra", "-Wno-unused-parameter",
        # GCC 14 起, C99 模式下这几项默认升级为 error。GD32 标准库和 LVGL 8.3
        # 是较老的 C 代码, 显式降级回 warning, 避免新工具链第一次编译就被挡住。
        "-Wno-error=implicit-function-declaration",
        "-Wno-error=implicit-int",
        "-Wno-error=int-conversion",
        "-Wno-error=incompatible-pointer-types"]
INCLUDES = [
    "-Iconfig",
    "-Icommon/bsp", "-Icommon/protocol", "-Icommon/boot_shared",
    "-Iapp/src", "-Iboot/src",
    "-I" + CMSIS, "-I" + GDINC, "-I" + PERIPH + "/Include",
    "-I" + RTOS + "/include", "-I" + RTOS + "/portable/GCC/ARM_CM4F",
    "-I" + LVGL,
]
CFLAGS = (MCU + DEFS + INCLUDES + OPT + WARN +
          ["-fdata-sections", "-ffunction-sections", "-std=gnu99", "-MMD", "-MP"])
ASFLAGS = (MCU + DEFS + INCLUDES + OPT +
           ["-Wall", "-x", "assembler-with-cpp", "-fdata-sections",
            "-ffunction-sections", "-MMD", "-MP"])
LDFLAGS = (MCU + ["-specs=nano.specs", "-specs=nosys.specs",
                  "-Wl,--gc-sections", "-Wl,--print-memory-usage",
                  # GNU ld >= 2.39 会因裸机链接脚本把 FLASH 段标成 RWX 而告警；
                  # 这是嵌入式 ELF 的正常现象，不是错误，显式关闭该告警保持输出干净。
                  "-Wl,--no-warn-rwx-segments",
                  "-lc", "-lm", "-lgcc", "-lnosys"])

# -------------------------------------------------------------------- 源文件
def glob_rel(pattern: str) -> list[str]:
    return sorted(p.relative_to(ROOT).as_posix()
                  for p in ROOT.glob(pattern) if p.is_file())


GD32_C = glob_rel(PERIPH + "/Source/*.c") + [GDSRC + "/system_gd32f30x.c"]

COMMON_C = [
    "common/bsp/bsp_board.c",
    "common/bsp/bsp_uart.c",
    "common/bsp/bsp_spi.c",
    "common/bsp/bsp_lcd.c",
    "common/bsp/bsp_flash.c",
    "common/bsp/syscalls.c",
    "common/protocol/crc.c",
    "common/protocol/protocol.c",
    "common/boot_shared/boot_metadata.c",
    "common/boot_shared/boot_policy.c",
    "common/boot_shared/boot_jump.c",
]

BOOT_C = ["boot/src/main.c", "boot/src/systick.c"] + COMMON_C + GD32_C
APP_C = (glob_rel("app/src/*.c") + COMMON_C +
         ["common/bsp/bsp_touch.c"] + GD32_C)

RTOS_C = [
    RTOS + "/tasks.c", RTOS + "/queue.c", RTOS + "/list.c",
    RTOS + "/portable/GCC/ARM_CM4F/port.c",
    RTOS + "/portable/MemMang/heap_4.c",
]

LVGL_C = []
for pattern in ("src/*.c", "src/*/*.c", "src/*/*/*.c", "src/*/*/*/*.c"):
    LVGL_C += glob_rel(LVGL + "/" + pattern)
LVGL_C = sorted(set(LVGL_C))

COMMANDS = [
    "check", "test", "all", "boot", "app_a", "app_b", "factory",
    "packages", "clean", "probe", "flash-factory", "flash-boot",
    "flash-app-a", "version-status", "help",
]


# ------------------------------------------------------------------ 工具定位
# winget / NSIS 安装器的常见落点。安装时若没勾选 "Add to PATH", 这里也能找到。
GCC_DIR_GLOBS = [
    r"C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin",
    r"C:\Program Files\Arm GNU Toolchain arm-none-eabi\*\bin",
    r"C:\Program Files (x86)\GNU Arm Embedded Toolchain\*\bin",
    r"C:\Program Files\GNU Arm Embedded Toolchain\*\bin",
    os.path.expandvars(r"%LOCALAPPDATA%\Programs\Arm GNU Toolchain arm-none-eabi\*\bin"),
    r"C:\Tools\arm-gnu-toolchain\bin",
    r"C:\gcc-arm\bin",
    r"C:\Tools\gcc-arm\bin",
    r"C:\Program Files (x86)\GNU Tools ARM Embedded\*\bin",
    r"C:\xpack-arm-none-eabi-gcc*\bin",
    # STM32CubeIDE 自带 GNU Tools for STM32，也是通用 arm-none-eabi，可直接编译 GD32F303
    r"D:\STM32CubeIDE_*\STM32CubeIDE\plugins\*gnu-tools-for-stm32*\tools\bin",
    r"C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\*gnu-tools-for-stm32*\tools\bin",
    r"C:\Program Files\STMicroelectronics\STM32CubeIDE_*\STM32CubeIDE\plugins\*gnu-tools-for-stm32*\tools\bin",
]


def autodetect_gcc_dir() -> str | None:
    for pattern in GCC_DIR_GLOBS:
        for candidate in sorted(glob.glob(pattern), reverse=True):
            if (Path(candidate) / (PREFIX + "gcc.exe")).is_file():
                return candidate
    return None


class Toolchain:
    """查找顺序: --gcc-path > ARM_GCC_PATH > config/toolchain_path.txt > PATH > 常见安装目录。

    PATH 里能找到时直接用 which(), 不锁定目录; 否则自动探测 winget/NSIS 的默认落点。
    """

    def __init__(self, gcc_path: str | None):
        path = gcc_path or os.environ.get("ARM_GCC_PATH")
        if not path:
            cfg = ROOT / "config" / "toolchain_path.txt"
            if cfg.is_file():
                lines = [x.strip() for x in
                         cfg.read_text(encoding="utf-8").splitlines()]
                path = next((x for x in lines if x and not x.startswith("#")), None)
        if not path and shutil.which(PREFIX + "gcc"):
            pass                      # PATH 可用, exe() 会走 which()
        elif not path:
            path = autodetect_gcc_dir()
        self.bin = Path(path) if path else None

    def exe(self, name: str) -> str:
        if self.bin:
            for candidate in (self.bin / (PREFIX + name + ".exe"),
                              self.bin / (PREFIX + name)):
                if candidate.is_file():
                    return str(candidate)
            return str(self.bin / (PREFIX + name))
        return shutil.which(PREFIX + name) or (PREFIX + name)


# --------------------------------------------------------------- OpenOCD 定位
# STM32CubeIDE 自带 OpenOCD 0.12，但 scripts 目录不在 bin 旁边，而在另一个
# plugin 里，必须用 -s 显式指给 openocd，否则 [find interface/...] 找不到。
OCD_BIN_GLOBS = [
    r"D:\STM32CubeIDE_*\STM32CubeIDE\plugins\*externaltools.openocd*\tools\bin",
    r"C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\*externaltools.openocd*\tools\bin",
    r"C:\Program Files\STMicroelectronics\STM32CubeIDE_*\STM32CubeIDE\plugins\*externaltools.openocd*\tools\bin",
    r"C:\Program Files\STMicroelectronics\STM32CubeIDE_*\plugins\*externaltools.openocd*\tools\bin",
    r"C:\Program Files\xpack-dev-tools\openocd*\bin",
    r"C:\xpack\openocd*\bin",
    r"C:\Tools\openocd\bin",
]
OCD_SCRIPTS_GLOBS = [
    r"D:\STM32CubeIDE_*\STM32CubeIDE\plugins\*debug.openocd*\resources\openocd\st_scripts",
    r"C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\*debug.openocd*\resources\openocd\st_scripts",
    r"C:\Program Files\STMicroelectronics\STM32CubeIDE_*\STM32CubeIDE\plugins\*debug.openocd*\resources\openocd\st_scripts",
    r"C:\Program Files\xpack-dev-tools\openocd*\share\openocd\scripts",
    r"C:\xpack\openocd*\share\openocd\scripts",
]


def _first_glob(patterns: list[str], must_have: str | None = None) -> str | None:
    for pattern in patterns:
        for candidate in sorted(glob.glob(pattern), reverse=True):
            if must_have and not (Path(candidate) / must_have).exists():
                continue
            return candidate
    return None


class OpenOCD:
    """定位 openocd.exe 与其 scripts 目录。

    顺序: --openocd-path > 环境变量 OPENOCD_PATH > config/openocd_path.txt
          > PATH > 自动探测 STM32CubeIDE / xPack 安装目录。
    """

    def __init__(self, ocd_path: str | None):
        bin_dir = ocd_path or os.environ.get("OPENOCD_PATH")
        if not bin_dir:
            cfg = ROOT / "config" / "openocd_path.txt"
            if cfg.is_file():
                lines = [x.strip() for x in
                         cfg.read_text(encoding="utf-8").splitlines()]
                bin_dir = next((x for x in lines if x and not x.startswith("#")), None)
        self.bin = Path(bin_dir) if bin_dir else None

        if self.bin and (self.bin / "openocd.exe").is_file():
            self.exe = str(self.bin / "openocd.exe")
        elif shutil.which("openocd"):
            self.exe = shutil.which("openocd")
        else:
            found = _first_glob(OCD_BIN_GLOBS, "openocd.exe")
            self.exe = str(Path(found) / "openocd.exe") if found else "openocd"

        # scripts 目录：优先自动探测；随 openocd.exe 安装的独立版在 ../share
        self.scripts = _first_glob(OCD_SCRIPTS_GLOBS, "interface/cmsis-dap.cfg")
        if not self.scripts:
            sibling = Path(self.exe).parent.parent / "share" / "openocd" / "scripts"
            if (sibling / "interface" / "cmsis-dap.cfg").is_file():
                self.scripts = str(sibling)

    def base_cmd(self, cfg: str | None = None) -> list[str]:
        cmd = [self.exe]
        if self.scripts:
            cmd += ["-s", self.scripts]
        cmd += ["-f", cfg or "config/openocd_gd32f303.cfg"]
        return cmd

    def describe(self) -> str:
        return ("%s  (scripts: %s)" % (self.exe, self.scripts or "未找到，将依赖 PATH")
                )


def run(cmd: list[str], dry: bool) -> None:
    printable = " ".join(str(x) for x in cmd)
    print("+ " + printable, flush=True)
    if dry:
        return
    subprocess.run([str(x) for x in cmd], cwd=str(ROOT), check=True)


def obj_for(target: str, source: str) -> Path:
    return BUILD / target / "obj" / (source[:-2] + ".o")


def compile_sources(target: str, sources: list[str], defs: list[str],
                    tc: Toolchain, dry: bool) -> list[Path]:
    objects: list[Path] = []
    obj_dir = BUILD / target / "obj"
    # 编译参数（版本号/-D 宏）变化时必须整目录重编，否则会复用旧目标文件，
    # 出现"版本号已经改了但固件里还是旧版本"的问题
    flags_id = hashlib.sha1(
        " ".join(CFLAGS + ASFLAGS + defs).encode("utf-8")).hexdigest()
    stamp = obj_dir / "compile.flags"
    force = True
    if not dry:
        obj_dir.mkdir(parents=True, exist_ok=True)
        if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == flags_id:
            force = False
        else:
            for old in obj_dir.glob("*.o"):
                old.unlink()
            stamp.write_text(flags_id + "\n", encoding="utf-8")
    for src in sources:
        obj = obj_for(target, src)
        src_abs = ROOT / src
        if not dry:
            obj.parent.mkdir(parents=True, exist_ok=True)
        if force or not (obj.is_file() and
                         obj.stat().st_mtime >= src_abs.stat().st_mtime):
            run([tc.exe("gcc"), "-c"] + CFLAGS + defs + [src, "-o",
                obj.relative_to(ROOT).as_posix()], dry)
        objects.append(obj)

    startup_obj = BUILD / target / "obj" / "startup.o"
    if not dry:
        startup_obj.parent.mkdir(parents=True, exist_ok=True)
    if force or not (startup_obj.is_file() and
                     startup_obj.stat().st_mtime >= (ROOT / STARTUP).stat().st_mtime):
        run([tc.exe("gcc"), "-c"] + ASFLAGS + defs + [STARTUP, "-o",
            startup_obj.relative_to(ROOT).as_posix()], dry)
    objects.append(startup_obj)
    return objects


def link_image(target: str, objects: list[Path], ld_script: str, elf: str,
               binary: str, tc: Toolchain, dry: bool) -> None:
    map_file = "build/%s/%s.map" % (target, "boot" if target == "boot" else "app")
    if not dry:
        (ROOT / elf).parent.mkdir(parents=True, exist_ok=True)
    run([tc.exe("gcc")] + [o.relative_to(ROOT).as_posix() for o in objects] +
        LDFLAGS + ["-T", ld_script, "-Wl,-Map=" + map_file + ",--cref",
                   "-o", elf], dry)
    run([tc.exe("size"), elf], dry)
    run([tc.exe("objcopy"), "-O", "binary", "-S", elf, binary], dry)


# ------------------------------------------------------- 产物版本记录
# 目的：让"写在元数据/固件头里的版本号"与"App 二进制里 APP_VERSION_* 宏"可核对。
# 没有这层记录时，--version 只改包头的文本，烧进去的仍是旧固件，
# 表现就是"升级完屏幕还是 v1.0"，也是这套演示最容易被误判为失败的地方。
def read_meta(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def sha1_of(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def write_image_meta(target: str, version: str, binary: Path, dry: bool) -> None:
    """记录某个 App 二进制是用哪个版本编译的（含链接脚本关键参数）。"""
    if dry or not binary.is_file():
        return
    BUILD.mkdir(parents=True, exist_ok=True)
    data = {
        "version": version,
        "binary": binary.name,
        "sha1": sha1_of(binary),
        "app_version_defs": version_defs(version)[0],
    }
    (BUILD / ("%s.meta.json" % target)).write_text(
        json.dumps(data, indent=2) + "\n", encoding="utf-8")


def read_image_version(target: str, binary: Path) -> str | None:
    """返回该二进制被记录的版本；文件被替换/校验不符时返回 None。"""
    meta = read_meta(BUILD / ("%s.meta.json" % target))
    if (meta.get("binary") == binary.name and binary.is_file() and
            meta.get("sha1") == sha1_of(binary)):
        return meta.get("version")
    return None


def write_factory_meta(version: str, dry: bool) -> None:
    if dry or not (BUILD / "factory.bin").is_file():
        return
    BUILD.mkdir(parents=True, exist_ok=True)
    data = {
        "version": version,
        "app_a_binary": "app_a.bin",
        "app_a_sha1": sha1_of(BUILD / "app_a.bin"),
        "factory_sha1": sha1_of(BUILD / "factory.bin"),
    }
    FACTORY_META.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def factory_version_matches(version: str) -> bool:
    """factory.bin 是否确实是用 version 构建的（用于不可逆烧录前拦截）。"""
    meta = read_meta(FACTORY_META)
    factory = BUILD / "factory.bin"
    app_a = BUILD / "app_a.bin"
    if not (factory.is_file() and app_a.is_file()):
        return False
    return (meta.get("version") == version and
            meta.get("factory_sha1") == sha1_of(factory) and
            meta.get("app_a_sha1") == sha1_of(app_a))


# ---------------------------------------------------------------------- 目标
def version_defs(version: str) -> tuple[list[str], list[str]]:
    parts = version.split(".")
    if (len(parts) != 2 or not all(p.isdigit() for p in parts) or
            not all(0 <= int(p) <= 65535 for p in parts)):
        raise SystemExit(
            "ERROR: --version 必须是 MAJOR.MINOR，且每段是 0..65535 的整数，"
            "收到 %r。\n       例如: --version 1.0 / --version 1.1 / --version 2.0"
            % version)
    major, minor = parts
    common = ["-DAPP_VERSION_MAJOR=" + major, "-DAPP_VERSION_MINOR=" + minor]
    return common, ["-DAPP_SLOT_B"] + common


def cmd_check(tc: Toolchain, ocd: OpenOCD, dry: bool) -> None:
    gcc = tc.exe("gcc")
    if dry:
        print("+ (check) %s --version" % gcc)
        return
    try:
        out = subprocess.run([gcc, "--version"], capture_output=True,
                             text=True, check=True)
        print("OK:", out.stdout.splitlines()[0])
        print("   compiler:", gcc)
        print("   objcopy :", tc.exe("objcopy"))
        if tc.bin:
            print("   (from toolchain dir:", str(tc.bin) + ")")
    except (OSError, subprocess.CalledProcessError):
        print("ERROR: %s not found." % gcc, file=sys.stderr)
        print("", file=sys.stderr)
        print("本机还没有 Arm GNU Toolchain。在【普通 PowerShell】里执行：", file=sys.stderr)
        print("  winget install --id Arm.GnuArmEmbeddedToolchain --source winget -e "
              "--accept-package-agreements --accept-source-agreements", file=sys.stderr)
        print("装完【完全退出并重启 VS Code】, 再跑一次本任务。", file=sys.stderr)
        print("若安装时没勾选 Add to PATH: 脚本会自动探测常见安装目录; 也可设环境变量", file=sys.stderr)
        print("ARM_GCC_PATH 指向 bin 目录, 或把路径写进 config/toolchain_path.txt。", file=sys.stderr)
        print("详见 docs/01_environment_setup.md", file=sys.stderr)
        raise SystemExit(1)
    print("NOTE: make / Git Bash are NOT required by this build script.")
    ocd_exe = Path(ocd.exe)
    ocd_ok = ocd_exe.is_file() or shutil.which("openocd")
    print(("OK:   OpenOCD  " if ocd_ok else "WARN: OpenOCD  未找到 ") +
          ocd.describe())
    if not ocd_ok:
        print("     烧录前请把 openocd.exe 的 bin 目录写进 config/openocd_path.txt，")
        print("     或设环境变量 OPENOCD_PATH，或用 --openocd-path 指定。")


def cmd_test(tc: Toolchain, dry: bool) -> None:
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"], dry)


def build_boot(tc: Toolchain, dry: bool, allow_downgrade: bool = False) -> None:
    print("== Bootloader ==")
    boot_defs = ["-DBOOT_BUILD=1"]
    if allow_downgrade:
        boot_defs.append("-DBOOT_ALLOW_DOWNGRADE=1")
    objs = compile_sources("boot", BOOT_C, boot_defs, tc, dry)
    link_image("boot", objs, "linker/boot.ld", "build/boot/boot.elf",
               "build/bootloader.bin", tc, dry)


def build_app(slot: str, version: str, tc: Toolchain, dry: bool) -> None:
    target = "app_a" if slot == "A" else "app_b"
    print("== App-%s (v%s) ==" % (slot, version))
    common_defs, b_defs = version_defs(version)
    defs = common_defs if slot == "A" else b_defs
    sources = APP_C + RTOS_C + LVGL_C
    objs = compile_sources(target, sources, defs, tc, dry)

    origin = "0x08005000" if slot == "A" else "0x08080000"
    length = "0x0007B000" if slot == "A" else "0x00080000"
    ld = "build/%s/app.ld" % target
    if not dry:
        (ROOT / ld).parent.mkdir(parents=True, exist_ok=True)
    run([tc.exe("gcc"), "-E", "-P", "-x", "c",
         "-DAPP_FLASH_ORIGIN=" + origin, "-DAPP_FLASH_LENGTH=" + length,
         "linker/app.ld", "-o", ld], dry)
    link_image(target, objs, ld, "build/%s/app.elf" % target,
               "build/%s.bin" % target, tc, dry)
    write_image_meta(target, version, BUILD / ("%s.bin" % target), dry)


def build_factory(version: str, dry: bool) -> None:
    print("== factory.bin ==")
    # 语义守卫：factory.bin 里的元数据版本号由 --version 决定，而屏幕显示的
    # 版本号来自 App-A 二进制里的 APP_VERSION_* 宏。两者不一致时，
    # 烧进去会看到"元数据 v1.1 / 屏幕 v1.0"，直接破坏版本演示。
    actual = read_image_version("app_a", BUILD / "app_a.bin")
    if not dry and actual != version:
        raise SystemExit(
            "ERROR: build/app_a.bin %s，但 --version 指定 v%s。\n"
            "       factory.bin 会把元数据写成 v%s，屏幕却仍显示另一个版本。\n"
            "       请先执行: python tools/build.py --version %s app_a\n"
            "       或直接用: python tools/build.py --version %s all（重新编译两个槽）"
            % ("是 v%s" % actual if actual else "没有版本记录（可能被替换或从未用本脚本编译）",
               version, version, version, version))
    run([sys.executable, "tools/factory_image.py",
         "--boot", "build/bootloader.bin",
         "--app-a", "build/app_a.bin",
         "--version", version, "--output", "build/factory.bin"], dry)
    write_factory_meta(version, dry)


def build_packages(version: str, dry: bool) -> None:
    print("== OTA packages ==")
    # .fw 头的版本号来自 --version，镜像内容来自 build/app_*.bin。
    # 两者不一致会发出"标着 v1.2、刷进去屏幕却是 v1.1"的包，必须拦住。
    for slot in ("A", "B"):
        target = "app_%s" % slot.lower()
        binary = BUILD / ("%s.bin" % target)
        if not dry:
            actual = read_image_version(target, binary)
            if actual != version:
                raise SystemExit(
                    "ERROR: build/%s.bin %s，但 --version 指定 v%s。\n"
                    "       .fw 头会写 v%s 而镜像仍是另一个版本，刷进去屏幕不会变。\n"
                    "       请先执行: python tools/build.py --version %s all"
                    % (binary.name,
                       "是 v%s" % actual if actual else "没有版本记录",
                       version, version, version))
    if not dry:
        (ROOT / "build/packages").mkdir(parents=True, exist_ok=True)
    for slot, binary in (("A", "build/app_a.bin"), ("B", "build/app_b.bin")):
        run([sys.executable, "tools/fwpack.py", "pack",
             "--input", binary,
             "--output", "build/packages/app_%s_v%s.fw" % (slot.lower(), version),
             "--slot", slot, "--version", version,
             "--manifest", "build/packages/manifest.json"], dry)
        run([sys.executable, "tools/fwpack.py", "verify",
             "--package", "build/packages/app_%s_v%s.fw" % (slot.lower(), version),
             "--slot", slot], dry)


def flash(binary: str, address: str, args, ocd: OpenOCD, dry: bool) -> None:
    # 高级用法：显式指定 iface/target 任一个时，退回原始两文件模式（缺省项补默认值）
    iface = getattr(args, "openocd_iface", None)
    target = getattr(args, "openocd_target", None)
    if iface or target:
        cmd = [ocd.exe]
        if ocd.scripts:
            cmd += ["-s", ocd.scripts]
        cmd += ["-f", iface or "interface/cmsis-dap.cfg",
                "-f", target or "target/stm32f1x.cfg"]
        # 老配置没设 CPUTAPID，补一句让 GD32F303(Cortex-M4) 能连上
        cmd += ["-c", "set CPUTAPID 0x2ba01477"]
    else:
        cmd = ocd.base_cmd(args.openocd_cfg)
    run(cmd + ["-c", "program %s %s verify reset exit" % (binary, address)], dry)


def factory_swd_slice(dry: bool) -> None:
    """OpenOCD 的 stm32f1x 驱动对 GD32 常只认出 512 KiB，1 MiB 整片会写越界。

    而 factory.bin 后半（Slot B）本来就是 0xFF 空片，不需要写。这里截出前
    512 KiB 专门给 SWD 烧录用，等价且更快、不会越界。
    """
    src = BUILD / "factory.bin"
    dst = BUILD / "factory_512k.bin"
    if dry:
        print("+ (slice) %s -> %s : first 512 KiB" % (src.name, dst.name))
        return
    if not src.is_file():
        raise SystemExit("缺少 build/factory.bin，请先运行: python tools/build.py factory")
    data = src.read_bytes()
    dst.write_bytes(data[:0x80000])
    print("SWD 烧录镜像: %s (%d bytes, 前 512 KiB；后半 Slot B 保持擦除态)"
          % (dst.name, min(len(data), 0x80000)))


def cmd_probe(ocd: OpenOCD, dry: bool) -> None:
    """只连目标、读 IDCODE 和 Flash 容量，不擦不写。接线后先跑这个。"""
    print("== SWD 连接探测（不擦不写）==")
    print("OpenOCD:", ocd.describe())
    cmd = ocd.base_cmd() + [
        "-c", "init; targets; echo \"--- flash banks ---\"; flash list; shutdown"]
    run(cmd, dry)


def ensure_factory_for_flash(version: str, tc: Toolchain, dry: bool,
                             allow_downgrade: bool = False) -> None:
    """flash-factory 前保证镜像真的是 --version 指定的版本。

    factory.bin 是不可逆烧录的目标，不能拿着上一次 --version 1.1 的产物
    去执行 --version 1.0 flash-factory（那会"假装回到 1.0"，实际屏幕还是 1.1）。
    """
    if factory_version_matches(version):
        print("出厂镜像已是 v%s（校验值与记录一致），无需重建。" % version)
        return
    print("== 按 --version %s 重建出厂镜像所依赖的产物 ==" % version)
    if not (BUILD / "bootloader.bin").is_file():
        build_boot(tc, dry, allow_downgrade)
    build_app("A", version, tc, dry)
    build_factory(version, dry)


def cmd_clean(dry: bool) -> None:
    print("+ rm -rf build/")
    if not dry and BUILD.is_dir():
        shutil.rmtree(BUILD)


HELP = """GD32F303VGT6 A/B OTA —— 免 make 构建

  python tools/build.py check                 检查 arm-none-eabi-gcc
  python tools/build.py test                  22 项主机自测（无需工具链/硬件）
  python tools/build.py all                   Boot + App-A + App-B + 出厂镜像 + OTA 包
  python tools/build.py --version 1.1 all     指定 App 版本号（真正编译进固件）
  python tools/build.py --version 1.0 flash-factory   重建并烧回最初的 v1.0
  python tools/build.py boot | app_a | app_b | factory | packages | clean
  python tools/build.py probe                 只探测 SWD 连接（不擦不写，接线后先跑）
  python tools/build.py flash-factory | flash-boot | flash-app-a
  python tools/build.py version-status       打印当前产物的版本/槽位状态（不碰硬件）
  python tools/build.py help                  本帮助

版本号规则：
  --version X.Y 会同时写入 .fw 头、factory 元数据、App 屏幕显示的 APP_VERSION_* 宏。
  factory / packages / flash-factory 会核对二进制是否真是该版本，不一致直接报错，
  杜绝"包标着 v1.2、刷进去屏幕还是 v1.1"。flash-factory 若版本不符会自动重建。

可选参数（放在子命令前）：
  --gcc-path DIR          工具链 bin 目录
  --openocd-path DIR      OpenOCD 的 bin 目录（默认自动探测 STM32CubeIDE 自带版）
  --openocd-cfg CFG       默认 config/openocd_gd32f303.cfg（GD32 + DAPLink 专用）
  --openocd-iface CFG     高级：覆盖接口配置
  --openocd-target CFG    高级：覆盖目标配置
  --dry-run               只打印命令，不执行

产物在 build/：
  bootloader.bin  app_a.bin  app_b.bin  factory.bin
  packages/manifest.json  packages/app_a_v*.fw  packages/app_b_v*.fw

不需要 make、不需要 Git Bash；PowerShell / CMD / Git Bash 终端都能直接跑。
Makefile 仍可用，两者参数一致。

工具链定位顺序：--gcc-path > 环境变量 ARM_GCC_PATH >
config/toolchain_path.txt(写一行 bin 路径) > PATH > 自动探测常见安装目录。
winget 装完即使没勾 "Add to PATH", 通常也能自动找到。
"""


def main() -> None:
    ap = argparse.ArgumentParser(
        description="GD32F303VGT6 A/B OTA build (no make required)",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="1.0", help="App version, e.g. 1.1")
    ap.add_argument("--gcc-path", default=None, help="Arm toolchain bin directory")
    ap.add_argument("--openocd-path", default=None, help="OpenOCD bin directory")
    ap.add_argument("--openocd-cfg", default="config/openocd_gd32f303.cfg")
    ap.add_argument("--openocd-iface", default=None)
    ap.add_argument("--openocd-target", default=None)
    ap.add_argument("--dry-run", action="store_true",
                    help="print commands without executing")
    ap.add_argument("--allow-downgrade", action="store_true",
                    help="允许 Boot 接收更低版本的 OTA 包（默认拒绝降级）")
    sub = ap.add_subparsers(dest="cmd")
    for name in COMMANDS:
        # 同一组全局参数在子命令前后都能写；SUPPRESS 保证不覆盖主 parser 的默认值
        sp = sub.add_parser(name)
        sp.add_argument("--version", default=argparse.SUPPRESS,
                        help="App version, e.g. 1.1")
        sp.add_argument("--gcc-path", default=argparse.SUPPRESS,
                        help="Arm toolchain bin directory")
        sp.add_argument("--openocd-path", default=argparse.SUPPRESS)
        sp.add_argument("--openocd-cfg", default=argparse.SUPPRESS)
        sp.add_argument("--openocd-iface", default=argparse.SUPPRESS)
        sp.add_argument("--openocd-target", default=argparse.SUPPRESS)
        sp.add_argument("--dry-run", action="store_true",
                        default=argparse.SUPPRESS,
                        help="print commands without executing")
        sp.add_argument("--allow-downgrade", action="store_true",
                        default=argparse.SUPPRESS,
                        help="允许 Boot 接收更低版本的 OTA 包")
    args = ap.parse_args()

    tc = Toolchain(args.gcc_path)
    ocd = OpenOCD(args.openocd_path)
    dry = args.dry_run
    cmd = args.cmd or "help"

    # 提前校验版本格式，避免"编到一半才报错"；help/probe/clean/test 不需要版本
    if cmd not in ("help", "check", "test", "clean", "probe", "version-status",
                   "flash-boot", "flash-app-a"):
        version_defs(args.version)

    if cmd == "help":
        print(HELP)
        return
    if cmd == "check":
        cmd_check(tc, ocd, dry)
        return
    if cmd == "test":
        cmd_test(tc, dry)
        return
    if cmd == "clean":
        cmd_clean(dry)
        return
    if cmd == "probe":
        cmd_probe(ocd, dry)
        return
    if cmd == "version-status":
        run([sys.executable, "tools/version_status.py"], dry)
        return

    if cmd == "flash-factory":
        # 未显式写 --version 时沿用上次记录的出厂版本，避免"漏写版本号"把
        # 刚编好的 v1.1 意外重建回 v1.0。显式写了才按指定版本重建（演示回退用）。
        if not any(a == "--version" or a.startswith("--version=")
                   for a in sys.argv[1:]):
            recorded = read_meta(FACTORY_META).get("version")
            if recorded:
                args.version = recorded
        cmd_check(tc, ocd, dry)
        ensure_factory_for_flash(args.version, tc, dry, args.allow_downgrade)
        print(">> 即将烧录出厂镜像 v%s 到 0x08000000（不可逆，会覆盖旧固件）"
              % args.version)
        factory_swd_slice(dry)
        flash("build/factory_512k.bin", "0x08000000", args, ocd, dry)
        return
    if cmd == "flash-boot":
        flash("build/bootloader.bin", "0x08000000", args, ocd, dry)
        return
    if cmd == "flash-app-a":
        flash("build/app_a.bin", "0x08005000", args, ocd, dry)
        return

    if cmd in ("all", "boot", "app_a", "app_b", "factory", "packages"):
        if cmd != "packages":
            cmd_check(tc, ocd, dry)
        if cmd in ("all", "boot"):
            build_boot(tc, dry, args.allow_downgrade)
        if cmd in ("all", "app_a"):
            build_app("A", args.version, tc, dry)
        if cmd in ("all", "app_b"):
            build_app("B", args.version, tc, dry)
        if cmd in ("all", "factory"):
            build_factory(args.version, dry)
        if cmd in ("all", "packages"):
            build_packages(args.version, dry)
        if cmd == "all" and not dry:
            print("\nBuild complete (version %s)." % args.version)
            print("Outputs: build/*.bin, build/packages/*.fw")
        return

    ap.error("unknown command: %s" % cmd)


if __name__ == "__main__":
    main()
