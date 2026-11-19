# 01 · 从零搭建 GD32 开发环境（Windows）

你现在是"一台什么都没装的电脑"，本文按**实际操作顺序**带你装完、编译出第一个固件、并烧进板子。
全程不需要买正版编译器。

> 🔧 **重要更新（免 make 路线）**：工程新增 `tools/build.py`，用纯 Python 完成全部构建，
> **不再需要安装 GNU Make，也不再需要 Git Bash**。`.vscode/` 里的任务全部改走它，
> PowerShell / CMD / Git Bash 任意终端都能跑。
> 若你在 VSCode 里看到 `shell 可执行文件…路径不存在` 或 `make: command not found`，
> 说明在看旧版说明——直接用本文档的新命令即可。
> **照着点、照着敲的逐步教程见 [06 · VS Code 一步步操作指南](06_vscode_guide.md)**。

> 建议先花 2 分钟读一遍 [00 · 项目总览](00_overview.md)，知道 Boot / App / 分区是什么。

---

## 0. 先搞清楚要装什么

| 组件 | 作用 | 不装会怎样 |
|------|------|------------|
| **Arm GNU Toolchain**（`arm-none-eabi-gcc`） | 把 C 代码编译成 Cortex-M4 能跑的机器码（交叉编译器） | 无法编译固件 |
| **Python 3.10+** | 构建脚本、生成升级包 `.fw`、整片镜像、主机侧自测、串口升级工具 | 无法编译/打包/测试 |
| **pyserial** | PC 串口升级工具 `serial_ota.py` 依赖 | 串口升级工具跑不起来 |
| **OpenOCD 或 pyOCD** | 通过 SWD 调试器把固件烧进 Flash | 有固件也烧不进去 |
| **调试器 + 其 USB 驱动** | GD-Link / ST-Link / DAPLink / J-Link | 同上 |
| **USB 转串口模块 + 驱动** | 看日志（PA9/PA10）、以及不用 Wi-Fi 就能测试 OTA | 看不到任何打印 |
| **VS Code（可选但强烈推荐）** | 看代码、一键编译、调试、串口监视 | 用记事本也能干，但很痛苦 |
| **ESP-IDF（可选）** | 编译 ESP32-S3 网关 | 只能先用 PC 串口工具验证 GD32 侧 |
| ~~GNU Make / Git Bash~~ | **已不需要**：`tools/build.py` 绕开了它们 | 仅当你主动想用 `Makefile` 时才需要 |

一句话：**最小可运行集合 = ARM 工具链 + Python + 一个 SWD 调试器**。

---

## 1. 装 VS Code 和终端

1. 下载安装 [VS Code](https://code.visualstudio.com/)，安装时勾选 **"添加到 PATH"**。
2. 打开工程时会提示安装推荐插件：`C/C++`、`Cortex-Debug`、`Serial Monitor`、`Python`，全部装。
3. **终端用哪个都行**：PowerShell / CMD / Git Bash 均可，构建脚本不依赖 Unix 命令。
   > 只有你想手动跑 `Makefile` 时才需要 Git Bash（`mkdir -p`/`rm -rf`/`find`）。
   > 本机 Git 实际装在 `D:\Git`（不是 `C:\Program Files\Git`），
   > 需要时 `Ctrl+Shift+P` → `Terminal: Select Default Profile` 选择 Git Bash。

> 打开项目：VS Code → 文件 → 打开文件夹 → 选择 `C:\Users\lin\Desktop\gd32ota`，并点"信任"。
> 工程已自带 `.vscode/`：16 个一键编译/烧录任务、F5 调试配置、三套 IntelliSense 配置、串口监视器推荐插件，
> **不需要手工切换终端，也不需要手写 `launch.json`**。
> 从打开文件夹开始、照着点照着敲的完整流程见 **[06 · VS Code 一步步操作指南](06_vscode_guide.md)**。

---

## 2. 装 Arm GNU Toolchain（最关键）

### 方式 2.1：winget（可选，本机**不需要**）

> ✅ **本机已用现成工具链解决**：你电脑上的 STM32CubeIDE 1.19.0 自带
> `arm-none-eabi-gcc` 13.3.1，路径已写入 `config/toolchain_path.txt`，**编译已实测通过**。
> 下面的 winget 方案只在换电脑 / 想用官方新版工具链时才需要；**GD32 不需要任何专用插件或 Keil**。

本机实测结论：**已装 `winget`，未装 Chocolatey**；Python 3.13 已装（VSCode 用的是
`C:\Users\lin\AppData\Local\Programs\Python\Python313\python.exe`），`pyserial 3.5` 也已装好。
所以**只需要装 ARM 交叉编译器这一样**。

用**管理员身份**打开 PowerShell（Win+X → "终端(管理员)"，**不是** VSCode 里的终端），执行：

```powershell
winget install --id Arm.GnuArmEmbeddedToolchain --source winget -e --accept-package-agreements --accept-source-agreements
```

- 包 ID 已实测：`Arm.GnuArmEmbeddedToolchain` = 14.2.Rel1，安装包文件名含 `arm-none-eabi`
- 会弹 **UAC 提示，点"是"**；下载约 200 MB
- 安装界面若出现 **"Add path to environment variable"**，**勾上**；忘了勾也没关系，见 2.3
- 装完**完全退出并重启 VS Code**

> 烧录阶段（阶段 5）再装 OpenOCD：
> `winget install --id xpack-dev-tools.openocd-xpack --source winget -e --accept-package-agreements --accept-source-agreements`

### 方式 2.2：手动安装（不用 winget）

1. 到 [Arm GNU Toolchain 下载页](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
   下载 Windows 版 **`arm-gnu-toolchain-...-mingw-w64-i686-arm-none-eabi.zip`**（约 200 MB）。
   > 也可以选 [xPack GNU Arm Embedded GCC](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases)，
   > 免安装、解压即用，对 Windows 更友好。
2. 解压到一个**没有空格和中文**的路径，例如 `C:\Tools\arm-gnu-toolchain\`。
3. 把里面的 `bin` 目录加入系统 PATH：
   - Win+R → `sysdm.cpl` → 高级 → 环境变量 → 系统变量 `Path` → 新建 → `C:\Tools\arm-gnu-toolchain\bin`
4. Python 3 本机已装（3.13），**可跳过**；换电脑重装时记得勾选 "Add python.exe to PATH"。

### 方式 2.3：装完 `check` 仍找不到工具链（PATH 没加上）

winget 的 NSIS 安装器默认**不一定**勾选 "Add to PATH"。三条兜底任选其一，
`build.py` 按下面的顺序自动查找：

1. **自动探测（通常无需任何操作）**：脚本会扫描
   `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin` 等常见安装目录，
   winget 的默认落点就在其中。装完先直接跑 `python tools/build.py check` 试试。
2. **环境变量**：新建用户变量 `ARM_GCC_PATH`，值填工具链的 `bin` 目录，例如
   `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin`，然后**重启 VS Code**。
3. **配置文件 / 命令行参数**：把 `bin` 路径写进 `config/toolchain_path.txt`（一行即可），
   或每次加参数 `--gcc-path`：

   ```bash
   python tools/build.py --gcc-path "C:/Tools/arm-gnu-toolchain/bin" all
   ```

> VSCode 的一键任务里是固定命令，所以优先用方案 1 或 2，不用改 `tasks.json`。

### 验证（务必做）

**装完必须完全退出 VS Code 再重新打开**（环境变量要重新加载），然后逐条执行：

```bash
arm-none-eabi-gcc --version     # winget 装的应打印 gcc version 14.2.Rel1
python --version                # 本机 3.13.1
arm-none-eabi-objcopy --version # 生成 .bin 用
```

仓库自带一条检查命令（会自动探测工具链目录，不依赖 PATH）：

```bash
python tools/build.py check
```

> 若这一步报 `not found`，按上面 **2.3** 的三条兜底处理。
> 若 `python` 报"不是内部或外部命令"：用 `py -3` 代替
> （把 `.vscode/tasks.json` 里的 `python` 统一换成 `py -3`）。

---

## 3. 装 Python 依赖

**本机已完成**：`pyserial 3.5` 已装进 VSCode 使用的 Python 3.13，可跳过本节。
换电脑或重装 Python 后执行：

```bash
python -m pip install --upgrade pip
python -m pip install pyserial       # 仅 PC 串口升级工具需要
```

`build.py` / `fwpack.py` / `factory_image.py` / `firmware_server.py` / 测试用例只用标准库，不需要额外包。

---

## 4. 装烧录工具（SWD）

> ✅ **本机实测结论：这一节你一个都不用装。**
> - **调试器**：你手上是 DAPLink（CMSIS-DAP，`VID_0D28:PID_0204`，HID 传输，自带串口 `COM4`），已实测连上芯片。
> - **OpenOCD**：STM32CubeIDE 1.19.0 自带 0.12.0，`config/openocd_path.txt` 已指向它，脚本还会自动加 `-s` 脚本目录。
> - **不需要 Keil / GD32 DFP / GD-Link 驱动 / Zadig / winget**。GD32F303 是 Cortex-M4，
>   通用 `arm-none-eabi-gcc` + CMSIS-DAP(SWD) 就够，这两样本机 STM32CubeIDE 全带。
> - 工程已配好专用 `config/openocd_gd32f303.cfg`（含 `CPUTAPID 0x2ba01477`）。
>   **直接照 [06 文档](06_vscode_guide.md) 阶段 4~5 接线 → 探测 → 烧录即可**，本节下面只是换电脑/通用参考。

### 4.1 硬件准备

任选一个 SWD 调试器，接到板子的 `SWDIO(PA13)`、`SWCLK(PA14)`、`GND`、`3.3V`（**不要接 5V**）：

| 调试器 | 说明 |
|--------|------|
| **GD-Link** | GD32 官方调试器，最稳 |
| **ST-Link V2 / V3** | 最常见，SWD 与 GD32 兼容 |
| **DAPLink / CMSIS-DAP** | 开源方案 |
| **J-Link** | 功能最全 |

> 工程默认用 **`config/openocd_gd32f303.cfg`**（已内含 DAPLink 的 HID 后端 + GD32F303 的
> `CPUTAPID 0x2ba01477`，并引用官方 `target/stm32f1x.cfg`）。**本机不用改。**
> 换调试器时再覆盖，例如 ST-Link：
> ```bash
> python tools/build.py flash-factory \
>     --openocd-iface interface/stlink.cfg --openocd-target target/stm32f1x.cfg
> ```
> 注意 GD32F303 是 Cortex-M4，裸用 `target/stm32f1x.cfg` 会因 IDCODE 不匹配连不上；
> 脚本在覆盖模式下会自动补 `set CPUTAPID 0x2ba01477`。
> 也可以用 pyOCD（见 4.3）。

### 4.2 OpenOCD

- 推荐 [xPack OpenOCD](https://github.com/xpack-dev-tools/openocd-xpack/releases)：解压后把 `bin` 加入 PATH；
- 或 [官方预编译包](https://github.com/openocd-org/openocd/releases)。

验证：

```bash
openocd --version
```

**USB 驱动**：ST-Link 用 Keil/STM32Cube 自带驱动即可；
CMSIS-DAP / 部分 ST-Link 在 OpenOCD 下需要用 [Zadig](https://zadig.akeo.ie/) 把接口换成 **WinUSB**。
现象是 `openocd` 报 `LIBUSB_ERROR_NOT_SUPPORTED` / `unable to open device`。

### 4.3 备选：pyOCD

```bash
python -m pip install pyocd
pyocd list                 # 应能看到调试器和目标芯片
```

pyOCD 对 GD32F303 的 target 支持随版本变化，若 `pyocd flash -t gd32f303vg` 报不支持，
就改用 OpenOCD，或用 `pyocd flash -t cortex_m build/factory.bin --base-address 0x08000000`。

### 4.4 USB 转串口模块（强烈建议接上）

- 模块：CH340 / CP2102 / FT232 均可，装对应驱动（CH340 用 WCH 官网驱动，CP210x 用 Silicon Labs 驱动）。
- 接线（**交叉、共地**）：
  ```
  USB-TTL TX  -> GD32 PA10 (USART0_RX)
  USB-TTL RX  <- GD32 PA9  (USART0_TX)
  USB-TTL GND -> GD32 GND            ← 必须共地，否则乱码
  ```
- 设备管理器里记下 COM 号（例如 `COM6`），串口工具参数：`115200, 8N1, 无流控`。
- 用 VS Code Serial Monitor 或 PuTTY 打开该串口，上电应看到：
  ```
  GD32F303 Boot 1.0, A/B OTA
  ```
  看不到 → 先检查共地、TX/RX 是否接反、BOOT0 是否为 0。

> 另有一路串口 USART1（PA2/PA3）专供 ESP32-S3，**不要**和 USB-TTL 接到一起。

---

## 5. 第一次编译

依赖已经随仓库放在 `third_party/`（GD32 标准库、FreeRTOS-Kernel V10.5.1、LVGL v8.3.11），
**不需要再联网下载**。

在 VSCode 终端（PowerShell / CMD / Git Bash 均可）执行：

```bash
python tools/build.py help     # 先看有哪些命令
python tools/build.py test     # 不需要硬件，22 项协议/CRC/分区自测，应全部 OK
python tools/build.py all      # 编 Boot / App-A / App-B / factory.bin / .fw 升级包
```

VSCode 里更简单：`Ctrl+Shift+B` 就是完整构建；`Ctrl+Shift+P` → `Tasks: Run Task` 可选其余 15 个任务。

第一次编译 LVGL（约 190 个 `.c`，A/B 各一遍）会花 1~3 分钟。成功后 `build/` 下应有：

```
bootloader.bin   app_a.bin   app_b.bin   factory.bin
packages/manifest.json
packages/app_a_v1.0.fw   packages/app_b_v1.0.fw
```

看尺寸：

```bash
arm-none-eabi-size build/boot/boot.elf build/app_a/app.elf build/app_b/app.elf
```

> **Boot 必须 ≤ 16 KiB**，`boot.ld` 里有 `ASSERT`，超了链接会直接报错。
> App 必须 ≤ 492 KiB (A 槽) / 512 KiB (B 槽)。

> 只打印命令不编译（排查参数用）：`python tools/build.py all --dry-run`，不需要工具链。
> 换版本号：`python tools/build.py --version 1.1 all`。

### 常见编译报错速查

| 报错 | 原因 / 处理 |
|------|-------------|
| `python: 不是内部或外部命令` | 装 Python 勾 Add to PATH，或把任务里的 `python` 换成 `py -3` |
| `arm-none-eabi-gcc: command not found` | PATH 问题，回到第 2 节；装完要**重启 VS Code** |
| `make: command not found` | 你在手动跑 `make`；改用 `tools/build.py`，或看第 9 节装 make |
| `'mkdir' 不是内部或外部命令` | 同上，`Makefile` 需要 Git Bash；`build.py` 没有这个问题 |
| `Please select the target GD32F30x device` | 没定义 `GD32F30X_XD`。构建脚本已带；Keil 用户看 `keil/README.md` |
| `undefined reference to __libc_init_array` | 链接参数缺 `-lc`；或工具链缺 newlib，重装完整 Arm GNU Toolchain |
| `multiple definition of _write / _sbrk` | 检查是否重复把 `syscalls.c` 加入工程；该文件函数已声明 `weak` |
| `region FLASH overflowed by ...` | Boot 超过 16 KiB：关掉不需要的 BSP、去掉 `-u_printf_float`、确认 `-Os` 与 `--gc-sections` 生效 |
| `lv_conf.h: No such file` | `-Iconfig` 没加；或 LVGL 版本不是 8.3.x |
| 改了头文件却没重新编译 | `python tools/build.py clean` 后再 `all`（增量只比对 `.c` 时间戳） |

---

## 6. 第一次烧录

接好 DAPLink（SWDIO→`PA13`、SWCLK→`PA14`、GND 共地、`BOOT0=0`），给板子上电：

```bash
# 0) 先探测，不擦不写；看到 Cortex-M4 r0p1 detected 即线接对了
python tools/build.py probe

# 1) 整片烧录（Boot + 有效元数据 + App-A），推荐首次使用
python tools/build.py flash-factory

# 或者分步烧（注意：单独烧不会写元数据区，首次仍建议 flash-factory）：
python tools/build.py flash-boot
python tools/build.py flash-app-a
```

`flash-factory` 等价于（脚本自动定位自带 OpenOCD 并加 `-s` 脚本目录）：

```bash
openocd -s <CubeIDE>/resources/openocd/st_scripts \
        -f config/openocd_gd32f303.cfg \
        -c "program build/factory_512k.bin 0x08000000 verify reset exit"
```

> 只烧前 512 KiB：OpenOCD 的 `stm32f1x` 驱动对 GD32 容量探测不准（实测报 512 KiB），
> 而 `factory.bin` 后半（Slot B）本就是 `0xFF` 空片，不需要写。脚本会自动生成 `factory_512k.bin`。

烧完复位，串口（**`COM4`**，115200 8N1）应看到 Boot 打印并跳转 App，屏幕点亮、显示主页面。
详细验证步骤见 [02 · 编译与烧录](02_build_and_flash.md) 和 [04 · 演示流程](04_ota_demo.md)。

> **重要**：`factory.bin` 是 1 MiB 整片镜像，**只能用于首次生产/救砖**。
> 日常升级发 `packages/*.fw`，绝不能把 `factory.bin` 当成 OTA 包发送。

---

## 7. 可选：VS Code 一键编译与调试

工程已自带完整的 `.vscode/` 配置，**不需要手写任何 JSON**：

| 文件 | 作用 |
|------|------|
| `settings.json` | UTF-8、文件关联、终端工作目录；**不硬编码任何 shell 路径** |
| `c_cpp_properties.json` | 三套 IntelliSense 配置：Boot / App-A / App-B，点状态栏可切换 |
| `tasks.json` | 17 个一键任务（全部走 `tools/build.py`）：检查工具链、自测、完整构建、单构建、**SWD 探测**、烧录、串口 OTA、固件服务器、dry-run |
| `launch.json` | Cortex-Debug：调试 Boot（launch）、附加 App-A / App-B（attach）；已配好本机 CubeIDE 自带的 openocd/gdb 路径 |
| `extensions.json` | 打开工程时推荐安装 C/C++、Cortex-Debug、Serial Monitor、Python |

常用快捷键：`Ctrl+Shift+B` = 完整构建；`Ctrl+Shift+P` → `Tasks: Run Task` 选其它任务；`F5` 开始调试。

`.vscode/launch.json` 已统一引用 `config/openocd_gd32f303.cfg`，**本机按 F5 即可，不用改**。
换电脑/换调试器时改里面的 `serverpath`、`searchDir`、`armToolchainPath` 三个绝对路径，
或改用对应的 `interface/*.cfg`。

> `svdFile` 可选：GD32F30x 的 `.svd` 不在本仓库内，需要从 GigaDevice 官网/DFP 包里
> 取到后自行填路径；不填也能断点调试，只是看不到外设寄存器视图。

> ⚠️ 调试 **App** 必须用 `attach`（配置里已提供 **"附加到 App-A / App-B (不复位 / 不擦 Flash)"**），
> **不要用 launch**：launch 会重新烧写并擦除扇区，可能破坏有效元数据。
> 正确顺序：先烧 `factory.bin` → 复位让 Boot 跳转 App → 再按 F5 选 attach。

详细步骤见 **[06 · VS Code 一步步操作指南](06_vscode_guide.md)** 阶段 0 与阶段 7。

---

## 8. 可选：ESP32-S3 侧环境（Wi-Fi 网关）

只在你要做真正的 Wi-Fi OTA 时才需要；在此之前用 PC 串口工具就能完整验证 GD32 侧。

1. 安装 **ESP-IDF v5.0+**：推荐用 [ESP-IDF Windows Installer](https://dl.espressif.com/dl/esp-idf/)
   （会自动装工具链、CMake、Ninja、Python 环境），或按
   [Espressif 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/) 手动装。
2. 打开 "ESP-IDF CMD" 终端（它会自动设置 `IDF_PATH` 和 PATH）。
3. 配置并编译：

```bash
cd esp32_s3
idf.py set-target esp32s3
idf.py menuconfig          # 进入 "GD32 OTA Gateway" 填 Wi-Fi / URL / UART 引脚
idf.py build
idf.py -p COMxx flash monitor
```

4. 详细接线与 URL 配置见 [03 · 引脚表](03_pinout.md) 与 `esp32_s3/README.md`。

> `menuconfig` 里 `OTA_WIFI_SSID` / `OTA_WIFI_PASSWORD` / `OTA_MANIFEST_URL` /
> `OTA_FIRMWARE_BASE_URL` 必须改成你自己的；IP 用 PC 在局域网里的地址，不能用 `127.0.0.1`。

---

## 9. 可选：标准 Makefile 路线 / Keil

### 9.1 Makefile（路线 B）

如果你更习惯标准 GCC + Make，或要在 Linux/CI 上构建，顶层 `Makefile` 完整保留，参数与 `build.py` 一致：

```bash
make check-toolchain && make test && make all
make VERSION=1.1 all
make flash-factory OPENOCD_IFACE=interface/stlink.cfg
```

**前提**：需要 `make` 和 Git Bash（Makefile 用了 `mkdir -p`、`rm -rf`、`find`）。
本机 Git 在 `D:\Git`，Git Bash 在 `D:\Git\bin\bash.exe`；`make` 需另外安装
（MSYS2 里 `pacman -S make`，或 `winget search make` 后自行选择）。
**路线 A（`build.py`）完全不需要 make**；本机 winget 可用但 Chocolatey 未安装，
所以不要照抄旧教程里的 `choco install`。
VSCode 里把默认终端切到 Git Bash 后再手动执行 `make ...`；一键任务不走这条路。

### 9.2 Keil MDK（已有许可证时）

见 **[keil/README.md](../keil/README.md)**，里面有：
- DFP 安装、`Options for Target` 逐项配置、三个 `.sct` 分散加载文件
- 必须定义的宏 `GD32F30X_XD, USE_STDPERIPH_DRIVER, HXTAL_VALUE=8000000`
- App-B 的 `APP_SLOT_B`
- MicroLIB + `fputc` 重定向
- `fromelf` 生成 `.bin`

Keil 只负责产出 `.bin`；打包、整片镜像、自测、串口升级仍用仓库里的 Python 工具。

---

## 10. 环境自检清单

全部打勾再进入下一步：

- [ ] `arm-none-eabi-gcc --version` / `python --version` 都正常（**不需要 make**）
- [ ] `python tools/build.py check` 通过
- [ ] `python tools/build.py test` → `OK (22 tests)`
- [ ] `python tools/build.py all` 成功，`build/` 下有 4 个 `.bin` 和 2 个 `.fw`
- [ ] `arm-none-eabi-size` 显示 Boot ≤ 16 KiB、App ≤ 槽容量
- [ ] USB-TTL 能看到 `GD32F303 Boot 1.0, A/B OTA`
- [ ] `python tools/build.py flash-factory` 成功，复位后屏幕显示主页面

任何一步卡住 → 先对照第 5 节的报错表，再看 [00 · 已知限制](00_overview.md#已知限制)。
