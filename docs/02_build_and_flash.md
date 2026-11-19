# 02 · 编译、链接与烧录

> 环境搭建见 [01 文档](01_environment_setup.md)。本文假设 `python tools/build.py check` 已通过。
>
> **本工程有两套等价构建入口**（参数与产物完全一致，共用 `build/` 目录）：
> - 路线 A（推荐，免 make）：`python tools/build.py all` —— 不需要 make / Git Bash，任意终端可跑
> - 路线 B（标准 GCC）：`make all` —— 需要 make + Git Bash
>
> 下文以 `make` 为例展示底层原理。**没装 make 时把 `make xxx` 换成 `python tools/build.py xxx`**：
> `make VERSION=1.1 all` → `python tools/build.py --version 1.1 all`；
> `make OPENOCD_CFG=config/xxx.cfg flash-factory` →
> `python tools/build.py flash-factory --openocd-cfg config/xxx.cfg`。
>
> **烧录/调试器**：本机用 STM32CubeIDE 1.19.0 自带的 OpenOCD 0.12 + DAPLink(CMSIS-DAP)，
> 全部工具已就位，配置见 `config/openocd_gd32f303.cfg` 与 [06 文档](06_vscode_guide.md) 阶段 4~5。

## 1. 一条命令出齐所有产物

```bash
cd gd32ota
python tools/build.py all                 # 路线 A：默认 VERSION=1.0
python tools/build.py --version 1.1 all    # 以 1.1 版本构建 App 与升级包

# 或者路线 B（需 make + Git Bash）：
make all
make VERSION=1.1 all
```

`Makefile` 会依次：

1. 编译 **Bootloader**（裸机）→ `build/bootloader.bin`
2. 编译 **App-A**（FreeRTOS + LVGL，链接 `0x08005000`）→ `build/app_a.bin`
3. 编译 **App-B**（同一套源码，`-DAPP_SLOT_B`，链接 `0x08080000`）→ `build/app_b.bin`
4. 生成 **出厂整片镜像** → `build/factory.bin`
5. 生成 **两个 OTA 包 + manifest** → `build/packages/`

只想要其中一个：

```bash
make boot | app_a | app_b | factory | packages
```

## 2. 产物清单

| 文件 | 说明 |
|------|------|
| `build/boot/boot.elf` `.map` | Boot ELF 与链接 map（看符号/段大小） |
| `build/app_a/app.elf` `.map` | App-A ELF 与 map |
| `build/app_b/app.elf` `.map` | App-B ELF 与 map |
| `build/bootloader.bin` | Boot 裸二进制，烧到 `0x08000000` |
| `build/app_a.bin` | App-A，烧到 `0x08005000` |
| `build/app_b.bin` | App-B，烧到 `0x08080000` |
| `build/factory.bin` | 1 MiB 整片镜像（Boot + 元数据 + App-A），烧到 `0x08000000` |
| `build/packages/app_a_v1.0.fw` | A 槽 OTA 包 = 40 B 头 + `app_a.bin` |
| `build/packages/app_b_v1.0.fw` | B 槽 OTA 包 |
| `build/packages/manifest.json` | 版本号 + A/B 包文件名，供 ESP32-S3 读取 |

看尺寸（**每次编译后都建议看一眼**）：

```bash
arm-none-eabi-size build/boot/boot.elf build/app_a/app.elf build/app_b/app.elf
ls -l build/*.bin build/packages/*
```

约束：Boot ≤ `0x4000` (16 KiB)、App-A ≤ `0x7B000` (492 KiB)、App-B ≤ `0x80000` (512 KiB)。
三个链接脚本里都有 `ASSERT`，超限会直接链接失败。

## 3. 链接配置（这是双工程的核心）

### GCC 路线

| 工程 | 链接脚本 | ORIGIN | LENGTH | 编译宏 |
|------|----------|--------|--------|--------|
| Boot | `linker/boot.ld` | `0x08000000` | `0x4000` | `-DBOOT_BUILD=1` |
| App-A | `linker/app.ld` 预处理生成 `build/app_a/app.ld` | `0x08005000` | `0x7B000` | 版本宏 |
| App-B | `linker/app.ld` 预处理生成 `build/app_b/app.ld` | `0x08080000` | `0x80000` | `-DAPP_SLOT_B` + 版本宏 |

`linker/app.ld` 用了 `#ifndef APP_FLASH_ORIGIN` 这样的条件宏，
GNU ld 本身不认识 C 预处理指令，所以 Makefile 会先用

```bash
arm-none-eabi-gcc -E -P -x c \
  -DAPP_FLASH_ORIGIN=0x08005000 -DAPP_FLASH_LENGTH=0x0007B000 \
  linker/app.ld -o build/app_a/app.ld
```

生成最终脚本再交给链接器。**不要直接把 `linker/app.ld` 传给 `-T`**。

全局编译宏（Makefile 已带）：

```
-mcpu=cortex-m4 -mthumb
-DUSE_STDPERIPH_DRIVER -DGD32F30X_XD -DHXTAL_VALUE=8000000
-Os -ffunction-sections -fdata-sections
-specs=nano.specs -specs=nosys.specs -Wl,--gc-sections
```

- `GD32F30X_XD`：1 MiB Flash 属于 XD 系列，**不定义会 `#error`**。
- `HXTAL_VALUE=8000000`：板上 8 MHz 晶振，PLL 才能算出 120 MHz。
- `nano.specs`：newlib-nano，Boot 16 KiB 能装下 printf 的关键；**不要**加 `-u_printf_float`。
- App 的向量表偏移：`app/src/main.c` 里
  `nvic_vector_table_set(NVIC_VECTTAB_FLASH, APP_VECTOR_OFFSET)`。

### Keil 路线

见 [keil/README.md](../keil/README.md)：三个 `.sct` 分散加载文件 + `Options for Target` 逐项配置。
关键宏相同，App-B 额外加 `APP_SLOT_B`。

## 4. 出厂镜像是怎么来的

```bash
python tools/factory_image.py \
    --boot build/bootloader.bin \
    --app-a build/app_a.bin \
    --version 1.0 \
    --output build/factory.bin
```

它生成固定 1 MiB 的镜像：

```
0x0000   Bootloader (≤16 KiB)
0x4000   元数据（magic/序列/槽信息/CRC32），App-A 标为 CONFIRMED
0x5000   App-A 原始镜像
其余      0xFF
```

**为什么需要它**：只烧 Boot + App-A 时元数据区是空白，Boot 会认为"无有效 App"而停在升级界面。
`factory.bin` 预置了正确 size/CRC 的元数据。App 自身也有"首次运行自登记"兜底，但用出厂镜像最省事。

> `factory.bin` **只能用于首次生产或救砖**，绝不能当 OTA 包发送：
> 它包含 Boot 区，OTA 流程不会擦 Boot，直接发会导致数据错位。

## 5. 烧录（SWD / DAPLink）

**本机已具备全部工具，不用装任何东西**：STM32CubeIDE 1.19.0 自带 OpenOCD 0.12 与
`arm-none-eabi` 工具链；`config/toolchain_path.txt`、`config/openocd_path.txt` 已指向它们。
调试器是 DAPLink（CMSIS-DAP，`VID_0D28:PID_0204`，HID 传输，自带串口 `COM4`）。

```bash
# 1) 接线后先探测，不擦不写；看到 Cortex-M4 r0p1 detected 即线接对了
python tools/build.py probe

# 2) 烧整片出厂镜像（自动截前 512 KiB，避免 GD32 容量探测不准导致越界）
python tools/build.py flash-factory

# 3) 只烧单个分区（改代码后调试用；元数据区不会被写，首次仍建议用 flash-factory）
python tools/build.py flash-boot
python tools/build.py flash-app-a
```

`flash-*` 实际执行的 OpenOCD 命令（脚本自动定位 exe 并加 `-s` 脚本目录）：

```bash
openocd -s <CubeIDE>/resources/openocd/st_scripts \
        -f config/openocd_gd32f303.cfg \
        -c "program build/factory_512k.bin 0x08000000 verify reset exit"
```

> **`config/openocd_gd32f303.cfg` 两处关键修正**（缺一不可）：
> ① `set CPUTAPID 0x2ba01477` —— GD32F303 是 Cortex-M4，官方 `target/stm32f1x.cfg`
> 默认按 M3 的 `0x1ba01477` 校验，不改会报 `expected 1 of 1: 0x1ba01477` 直接连不上；
> ② `cmsis-dap backend hid` —— 这款 DAPLink 走 HID 而非 WinUSB/Bulk。
> 此外 `stm32f1x` 驱动官方明确兼容 GD32F3 系列，只是容量寄存器读不准（实测报 512 KiB），
> 所以 `factory.bin` 只烧前 512 KiB —— 后半 Slot B 本来就是 `0xFF` 空片，完全等价。

换成别的调试器时（ST-Link 需先 Zadig 装 WinUSB）：

```bash
python tools/build.py flash-factory \
    --openocd-iface interface/stlink.cfg --openocd-target target/stm32f1x.cfg
```

Makefile 路线（等价，但需要 make）：

```bash
make flash-factory                              # 推荐：整片出厂镜像
make flash-boot                                 # 只烧 Boot
make flash-app-a                                # 只烧 App-A
make flash-factory OPENOCD_IFACE=interface/stlink.cfg
```

用 pyOCD（可选，需自行 `pip install pyocd`）：

```bash
pyocd flash -t cortex_m build/factory_512k.bin --base-address 0x08000000
```

### 烧录后应该看到什么

USB-TTL（115200 8N1）接 PA9/PA10：

```
GD32F303 Boot 1.0, A/B OTA
Jump slot A, v1.0, 123456 bytes

GD32F303 App 1.0, slot A
Application slot A confirmed
```

屏幕点亮 → 主页面显示版本号与联网状态。没有这些输出时：

1. 先确认 USB-TTL **共地**、TX/RX 交叉；
2. 确认 `BOOT0` 为 0（从主 Flash 启动）；
3. 确认 SWD 烧录时选的是 `.bin` 且地址正确（`factory.bin` 必须从 `0x08000000` 开始）；
4. 用 `openocd ... -c "mdw 0x08000000 4"` 读回前 8 字节，应等于 `bootloader.bin` 开头
   （MSP ≈ `0x20018000`，Reset_Handler 为奇数地址）。

## 6. 常见构建问题

| 现象 | 处理 |
|------|------|
| `region FLASH overflowed` | Boot 超限：确认 `-Os`、`--gc-sections`、没有 `-u_printf_float`；精简 BSP 或去掉 LCD 字库 |
| `undefined reference to __app_image_end` | App 链接脚本没用预处理版；确认 `make` 生成了 `build/app_a/app.ld` |
| `undefined reference to pvPortMalloc` | FreeRTOS 的 `heap_4.c` 没加入；Makefile 已含 |
| `lv_conf.h: No such file` | `-Iconfig` 缺失，或 LVGL 版本不是 8.3.x |
| `multiple definition of SysTick_Handler` | Boot 的 `systick.c` 被误加进 App；Makefile 中两者互斥 |
| App 跳转后 HardFault | 检查 `GD32F30X_XD`、VTOR、`nvic_vector_table_set`，以及跳转前是否恢复了全局中断 |
| 串口全乱码 | 8 MHz 晶振宏不对（`HXTAL_VALUE`），或波特率与 USB-TTL 不一致 |

## 7. 重新打包 / 换版本

```bash
# 只重新打 OTA 包（不重新编译固件）
make packages VERSION=1.1

# 单独生成、校验某个包
python tools/fwpack.py pack --input build/app_b.bin \
    --output build/packages/app_b_v1.1.fw --slot B --version 1.1 \
    --manifest build/packages/manifest.json
python tools/fwpack.py verify --package build/packages/app_b_v1.1.fw --slot B
```

## 8. 主机侧自测

```bash
make test
```

22 项测试覆盖 CRC16/CRC32 与 zlib 一致性、帧解析（粘包/错 CRC/超长/重同步）、
分区不重叠与页对齐、`.fw` 打包与篡改检测、`factory.bin` 布局与元数据 CRC、
以及 **GD32 C 头 / ESP32-S3 C 头 / Python 工具三方常量一致性**。不需要硬件。
