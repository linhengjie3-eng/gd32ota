# 06 · VS Code 一步一步操作指南

> 这份文档是给你**照着点、照着敲**的。按阶段顺序做，不要跳步。
> 工程已经放在：`C:\Users\lin\Desktop\gd32ota`。该路径没有空格、没有中文，交叉工具链可以正常处理。

---

## 🔧 如果你刚遇到「shell 可执行文件…路径不存在」

**已经修好了，不用你做任何事**，直接关掉报错的终端重新跑一次任务即可。原因是：

- 我最初把 Git Bash 写死成 `C:\Program Files\Git\bin\bash.exe`，但**你的 Git 装在 `D:\Git`**；
- 更关键的是实测发现你电脑上**根本没有 `make.exe`**，就算改对路径，下一步也会报 `make: command not found`。

所以工程新增了 **`tools/build.py`**：纯 Python 实现的构建入口，和 `Makefile` 参数完全一致，
**不需要 make、不需要 Git Bash**，PowerShell / CMD / Git Bash 任意终端都能跑。
`.vscode/` 里的 16 个任务已全部改走它，不再硬编码任何 shell 路径。

| 路线 | 命令 | 需要什么 | 适合 |
|------|------|----------|------|
| **A（默认，推荐）** | `python tools/build.py all` | Python 3 + ARM 工具链 | 新手、Windows、不想折腾环境 |
| B（标准 GCC） | `make all` | make + Git Bash + ARM 工具链 | 熟悉 Makefile、CI/Linux |

两条路线产物完全相同，`build/` 目录通用。本文全程用**路线 A**。

---

## 先看：现在到底是什么状态

| 项目 | 状态 |
|------|------|
| 全套源码（Boot / App / 协议 / BSP / ESP32-S3 / 工具） | ✅ 已就位 |
| 第三方库（GD32 标准库 / FreeRTOS 10.5.1 / LVGL 8.3.11） | ✅ 已随工程放进 `third_party/`，**不需要联网下载** |
| VSCode 配置（`.vscode/`：智能提示 / 16 个一键任务 / 调试 / 串口） | ✅ 已就位并修正 |
| 免 make 构建脚本 `tools/build.py` | ✅ 已就位；含工具链自动探测、GCC 兼容与 Cortex-M4F FPU 参数 |
| 文档、引脚表、Keil 分散加载文件、测试 | ✅ 已就位 |
| 主机侧 22 项自测 | ✅ **你已在 VSCode 里跑通**：`Ran 22 tests ... OK` |
| 本机 Python 3.13 + `pyserial 3.5` | ✅ 已装并验证（VSCode 用 `...\Python313\python.exe`），**无需再装** |
| **ARM 交叉编译器** | ✅ **已解决！用你电脑上 STM32CubeIDE 自带的 GNU Tools for STM32 13.3.1**，`config/toolchain_path.txt` 已指向它。**不用装 winget / Keil / 任何 GD32 插件** |
| **固件真正编译链接** | ✅ **已完成**：`Build complete (version 1.0)`，**0 error** |
| **可烧录镜像** | ✅ `build/factory.bin`(1 MiB)、`bootloader.bin`、`app_a/b.bin`、2 个 `.fw` 全部生成 |
| **烧录器 / 调试器** | ✅ **Flash Pro(CMSIS-DAP v2)，`VID_0D28:PID_0204`，串口 `COM4`**；已实测 `backend auto` 连上芯片 |
| **OpenOCD（烧录用）** | ✅ **不用装！** STM32CubeIDE 1.19.0 自带 OpenOCD 0.12，`config/openocd_path.txt` 已指向它 |
| `arm-none-eabi-gdb`（断点调试） | ✅ 同一个 STM32CubeIDE 工具链里自带，无需另装 |
| `make` / Git Bash | ⚪ **不需要**（路线 A 绕开）；你的 Git 实际在 `D:\Git` |
| **是否上板验证过** | ⚠️ SWD 已连通并识别到 Cortex-M4；**固件尚未写入，屏幕/串口尚未验证** |

一句话：**代码、固件、编译器、烧录器、烧录软件全都齐了，接线一插就能烧。**

> **你当前的进度**：阶段 0 ✅、阶段 1 ✅、阶段 2 ✅（22 项自测）、阶段 3 ✅（编译通过）。
> **DAPLink 实测已连上芯片**（`DPIDR 0x2ba01477`、`Cortex-M4 r0p1`、`Examination succeed`）。
> **下一步：阶段 4 接线 → 跑任务 17 探测 → 阶段 5 烧 `factory.bin`。**

### 🔌 已替你确认的三件事（省得你再折腾环境）

1. **GD32 不用下任何插件**：不需要 Keil、不需要 GD32 DFP、不需要 GD-Link 专用驱动、不需要 winget 装编译器或 OpenOCD。
   Cortex-M4 是通用内核，`arm-none-eabi-gcc` 编译、CMSIS-DAP/SWD 烧录，这两样 STM32CubeIDE 全自带。
2. **你这台电脑已经具备全部软件**：`check` 现在会同时打印编译器和 OpenOCD 两行 `OK`。
3. **看日志一根 TypeC 线就够**：板子自带 USB 转串口（已接 PA9/PA10），插电脑就多一个 COM 口，
   **USB-TTL 完全不用买**。DAPLink 自带的 `COM4` 只是备选（需额外接 TXD/RXD，别和板载串口并联）。

---

## 阶段 0 · 用 VS Code 打开工程 ✅ 已完成

1. 安装 [VS Code](https://code.visualstudio.com/)（安装时勾选"添加到 PATH"）。
2. 菜单 **文件 → 打开文件夹…**，选择 **`C:\Users\lin\Desktop\gd32ota`**。
   > 注意：选的是 `gd32ota` 这个文件夹**本身**，不要选它的上一级 `Desktop`。
3. 若弹出 **"是否信任此文件夹的作者"** → 选 **"信任"**（否则任务不能运行）。
4. 右下角若弹出 **"是否安装推荐的扩展"** → 点 **安装全部**。共 4 个：
   - `C/C++`（代码跳转、报错波浪线）
   - `Cortex-Debug`（SWD 断点调试）
   - `Serial Monitor`（看串口日志）
   - `Python`（PC 端工具脚本）
   > 没弹窗的话：左侧扩展图标 → 搜索框输入 `@recommended` → 逐个安装。
5. 按 **`Ctrl + \`**（反引号）打开集成终端。**用哪个终端都行**（PowerShell / CMD / Git Bash 均可），
   只要当前目录是工程根目录（`.vscode/settings.json` 已设好，不用再 `cd`）：
   ```
   PS C:\Users\lin\Desktop\gd32ota>        ← PowerShell，OK
   lin@DESKTOP MINGW64 /c/Users/lin/Desktop/gd32ota   ← Git Bash，也 OK
   ```
   > 以前这里强制要求 Git Bash，现在已经不需要了：`tools/build.py` 用 Python 建目录、
   > 用 `shutil` 清理，不调用 `mkdir -p` / `rm -rf` 等 Unix 命令。
6. 看 VSCode **右下角状态栏**，有一个 C/C++ 配置名（例如 `Boot (裸机, 0x08000000)`）。点它可以在三套配置间切换：
   - 改 `boot/` 下的代码 → 选 **Boot**
   - 改 `app/` 下的代码 → 选 **App-A** 或 **App-B**
   > 切换只是让代码跳转/宏高亮正确，**不影响编译**；编译由构建脚本决定。

✅ **过关判据**：左侧资源管理器能看到 `Makefile`、`.vscode`、`docs`、`tools`、`third_party`；终端能正常打开，不再报 shell 路径错误。

---

## 阶段 0.5 · 禁用 ESP-IDF 扩展 👈 打开工程后先做这一步

### 为什么要禁用

你贴过的这条报错：

```text
CMake Error: The source directory "C:/Users/lin/Desktop/gd32ota" does not appear to contain CMakeLists.txt.
```

**不是本工程的错，也不是代码不能跑**。原因是电脑上装的 **Espressif IDF** 扩展会把
**任何**打开的文件夹当成 ESP-IDF 工程，只要点了它状态栏的构建按钮（或它自动触发），
就会对整个 `gd32ota` 根目录跑 cmake —— 而本工程是 GD32 裸机 + FreeRTOS，
根目录本来就没有 `CMakeLists.txt`，所以必然报错。

GD32 的编译、烧录**完全不用**这个扩展，全部走 `python tools/build.py`。

### 怎么禁用（照着点，4 步）

1. 按 **`Ctrl + Shift + X`** 打开扩展面板（左侧竖排图标里那个"四方块"）。
2. 顶部搜索框输入 **`esp-idf`**（或 `@installed esp-idf`）。
3. 找到 **Espressif IDF**（发布者 `Espressif Systems`，扩展 ID `espressif.esp-idf-extension`），
   在列表里 **右键点它**。
4. 右键菜单里选 **「禁用(工作区)」 / Disable (Workspace)**。

> ⚠️ **别选错**：不要点「卸载」，也不要选「禁用」（那是全局）。
> 一定要选 **带"(工作区)"** 的那个 —— 它只对 `gd32ota` 这个文件夹生效，
> 以后你单独打开 `esp32_s3` 文件夹做 Wi-Fi OTA 时，这个扩展照常能用。

5. VSCode 若提示需要重新加载 → 点 **「重新加载窗口」/ Reload Window**；
   没弹提示就按 `Ctrl + Shift + P` → 输入 `Reload Window` → 回车。

✅ **过关判据**：VSCode **右下角那一排 ESP-IDF 小图标消失**（烧录 / 监视 / 端口 / SDK 配置等），
再跑任务也不会再弹 `CMake Error`。

### 暂时不想动扩展？也能不报错

- **什么都别点就行**：不要碰 ESP-IDF 状态栏的构建 / 烧录 / 配置按钮，也别用 CMake Tools 的构建。
  GD32 只用 **`Ctrl + Shift + P` → `Tasks: Run Task`** 跑任务 01~17。
- 工程侧已经替你加了三层保险，即使扩展还在也安静：
  - `.vscode/settings.json`：`cmake.configureOnOpen: false`、`cmake.automaticReconfigure: false`
    → CMake Tools 打开文件夹时不会自动配置；
  - `.vscode/extensions.json`：把该扩展列入 `unwantedRecommendations` → 不再推荐安装；
  - `.vscode/settings.json` 的 `//esp-idf` 注释里写清了原因，避免以后又误开。

> 以后要开发 ESP32-S3 网关时：**用 VSCode 单独打开 `esp32_s3` 文件夹**，
> 在那个窗口里把扩展启用（或重新装 ESP-IDF），互不干扰。

---

## 阶段 1 · 工具链 ✅ 已解决（你不用再装任何东西）

> **winget 那条命令不用执行了。** 实测发现你电脑上装了 **STM32CubeIDE 1.19.0**，
> 它自带完整的 `arm-none-eabi` 工具链（GNU Tools for STM32 **13.3.1**）。
> 这套编译器是通用 Cortex-M 工具链，**完全能编译 GD32F303**，与 ST/GD 芯片无关。

我已经把它的 `bin` 目录写进 **`config/toolchain_path.txt`**：

```
D:/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin
```

`build.py` 按 `--gcc-path` > `ARM_GCC_PATH` > `config/toolchain_path.txt` > PATH > 自动探测的顺序找到它，
**不用加 PATH、不用重启 VSCode、不用管理员权限**。任务 `01 · 检查工具链` 现在会打印：

```
OK: arm-none-eabi-gcc.exe (GNU Tools for STM32 13.3.rel1...) 13.3.1
   compiler: D:\STM32CubeIDE_1.19.0\...\tools\bin\arm-none-eabi-gcc.exe
```

> **结论：GD32 不需要任何专用插件、不需要 Keil、不需要 DFP 包、不需要再下载编译器。**
> Python 3 + 任意一个通用 `arm-none-eabi` 工具链就够了。

### 1.1 可选：以后想换成官方 Arm GNU Toolchain（现在不用做）

以**管理员身份**打开 PowerShell（Win+X → "终端(管理员)"）：

```powershell
winget install --id Arm.GnuArmEmbeddedToolchain --source winget -e --accept-package-agreements --accept-source-agreements
```

- 包 ID 已实测：`Arm.GnuArmEmbeddedToolchain` = **14.2.Rel1**，就是 `arm-none-eabi` 工具链
- 装完**完全退出并重启 VS Code**；若没勾 Add to PATH，脚本仍会自动探测常见安装目录

### 1.2 不用 winget：手动装（现在不用做）

| 组件 | 下载 / 安装 | 关键点 |
|------|-------------|--------|
| Arm GNU Toolchain | [Arm 官网](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 选 `...mingw-w64-i686-arm-none-eabi.zip`，或 [xPack 版](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases)（解压即用） | 解压到**无空格无中文**路径，把 `bin` 写进 `config/toolchain_path.txt` |
| Python 3.10+ | **本机已装 3.13.1，可跳过** | 换电脑时安装勾选 Add python.exe to PATH |

> **装完 `check` 仍找不到工具链？** 三条兜底任选其一：
> 1. 设用户环境变量 `ARM_GCC_PATH` = 工具链的 `bin` 目录（重启 VS Code）
> 2. 把 `bin` 路径写进 `config/toolchain_path.txt`（一行即可）← **本机用的就是这个**
> 3. 手动执行时加参数 `--gcc-path "C:/Tools/arm-gnu-toolchain/bin"`

### 1.3 什么时候需要重启 VS Code

**本机方案不需要**（脚本按绝对路径调用编译器，不依赖 PATH）。
只有你换了工具链、改了系统 PATH、或装了 OpenOCD 之后，才需要**完全退出 VS Code 再重开**。

### 1.4 验证（本机已通过）

`Ctrl+Shift+P` → `Tasks: Run Task` → **`01 · 检查工具链`**，期望看到 `OK: ... 13.3.1`。

`pyserial 3.5` 已替你装好并验证，换电脑时再执行 `python -m pip install pyserial`。

> 更详细的安装说明见 **[01 · 环境搭建](01_environment_setup.md)** 第 2、3 节。

---

## 阶段 2 · 先跑一次"不需要硬件"的自测 ✅ 已完成

你已跑完并贴给我看过：`Ran 22 tests in 0.340s / OK`
（等价命令：`python tools/build.py test`）。它验证 CRC16/CRC32、串口帧解析（粘包/错包/重同步）、
Flash 分区不重叠、`.fw` 打包与篡改检测、出厂镜像元数据，以及三方常量完全一致。

---

## 阶段 3 · 第一次真编译 ✅ 已完成

> 已实测通过：`Build complete (version 1.0)`，**0 error**（12 条无害 warning）。
> 实测占用：Boot **14,948 B / 16 KB (91.24%)**、App **134,488 B**（A 槽 26.69% / B 槽 25.65%）、
> App RAM **70,760 B / 96 KB (71.98%)**。**Boot 余量只剩约 1.1 KB**，后续加功能要注意。
> 产物：`build/factory.bin`(1 MiB)、`bootloader.bin`、`app_a.bin`、`app_b.bin`、2 个 `.fw`、`manifest.json`。

下面是以后改代码重新编译时用的操作说明：

按 **`Ctrl+Shift+B`**（等价于任务 **`03 · 完整构建 (build.py all)`**）。

构建脚本会依次编出：Boot → App-A → App-B → `factory.bin` → 两个 `.fw` 升级包 + `manifest.json`。
第一次编 LVGL（192 个 `.c`，A/B 各一遍）会慢一点，是正常的。

✅ **过关判据**：终端最后出现 `Build complete`，`build/` 下有 4 个 `.bin` 和 2 个 `.fw`。

再看一眼尺寸（**每次编译后都建议看**）：

```bash
arm-none-eabi-size build/boot/boot.elf build/app_a/app.elf build/app_b/app.elf
```

约束：**Boot ≤ 16 KiB、App-A ≤ 492 KiB、App-B ≤ 512 KiB**，链接脚本里有 `ASSERT`，超了会直接失败。

> **只打印命令、不编译**：任务 **`16 · 检查命令展开 (dry-run)`**，或 `python tools/build.py all --dry-run`。
> **改了头文件后必须 clean**：任务 **`09 · 清理 build`** 再编译（增量只比对 `.c` 时间戳，不追踪头文件）。

### 编译报错怎么在 VSCode 里看

任务配了 gcc 问题匹配器，编译错误会同时出现在 **"问题"面板**（`Ctrl+Shift+M`）。常见报错速查：

| 报错 / 现象 | 处理 |
|-------------|------|
| `arm-none-eabi-gcc: command not found` | 检查 `config/toolchain_path.txt` 的路径是否还存在 |
| `01 · 检查工具链` 报 `not found` | 同上；脚本也会自动探测 STM32CubeIDE / winget 安装目录 |
| `choco: 无法识别` | 本机没装 Chocolatey；本机也不需要装编译器 |
| `Please select the target GD32F30x device` | 缺 `GD32F30X_XD`，构建脚本已带；别自己改参数 |
| `lv_conf.h: No such file` | `-Iconfig` 丢了，或 LVGL 版本不对（工程内是 8.3.11） |
| `lv_mbox: lv_btnm is required` | `LV_USE_MSGBOX=1` 但 `LV_USE_BTNMATRIX=0`；工程已把 MSGBOX 设为 0 |
| `selected FPU does not support instruction` | FreeRTOS ARM_CM4F 需要 `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`；脚本已带 |
| `region FLASH overflowed` | Boot 超 16 KiB，见 [02 文档](02_build_and_flash.md) 第 6 节 |
| `undefined reference to pvPortMalloc` | FreeRTOS `heap_4.c` 没进工程（脚本已含） |
| `multiple definition of _write / _sbrk` | 重复加入了 `syscalls.c` |
| 改了头文件但没重新编 | 跑任务 **`09 · 清理 build`** 再编译 |

> 换工具链到 **GCC 14** 时，它对老 C 代码更严格（几项警告默认升级为 error）。
> `build.py` 已预置 `-Wno-error=implicit-function-declaration` 等兼容参数。

> 想换版本号（演示升级用）：终端敲 `python tools/build.py --version 1.1 all`，
> 不要用 `Ctrl+Shift+B`（它固定 1.0）。`--version` 写在子命令前后都支持。

---

## 阶段 4 · 接线（断电操作）👈 你现在在这一步

**⚠️ 全部断电操作；信号电平一律 3.3V；GD32 与每一个外设都必须共地。**

### 4.1 最小烧录/调试接线（DAPLink，先只接这几根就能烧）

> 你手上的调试器是 **Flash Pro(CMSIS-DAP v2)**，`VID_0D28:PID_0204`，自带串口 `COM4`。
> 烧录只接这 4 根线，**整排卡在板上专门的 10 针烧录区**即可；
> `PA13/PA14` 在 J2/J3 扩展排针上并没有引出，不用去那里找，也不要用杜邦线从别处引。
> **屏幕/触摸/蜂鸣器/ESP32 全部走 J2/J3 扩展排针**（对照表见 [03 文档](03_pinout.md) §7），
> 工程 `board_config.h` 与 J2/J3 一一对应，**一行都不用改**。
> 日志走板子 TypeC 自带的 USB 转串口，不用接 TXD/RXD。
> **以板上 10 针排针的丝印/网络标号为准**（`SWDIO`/`SWCLK`/`3V3`/`GND`）；
> 整排对插前断电用万用表确认两边 GND / 3V3 位置一致。

| DAPLink 丝印 | 接到 GD32 | 说明 |
|---|---|---|
| `SWDIO`（或 `DIO`/`DAT`） | `PA13` | 保留脚，严禁复用 |
| `SWCLK`（或 `CLK`） | `PA14` | 保留脚，严禁复用 |
| `GND` | `GND` | **必须共地**，不接必然连不上 |
| `3V3`（或 `VCC`） | `3.3V` | **绝对不要接 5V**；板子若已独立供电，此脚可不接 |
| `TXD` | `PA10`（USART0_RX） | **可不接**：板载 USB-UART 已连 PA9/PA10，日志走 TypeC 即可；要用 DAPLink 的 COM4 看日志才接（交叉） |
| `RXD` | `PA9`（USART0_TX） | 同上（交叉） |
| `RST`（若有） | `NRST` | 可选；不接也能烧，工程配置用的是软复位 |
| `BOOT0` | 保持 `0` | 从主 Flash 启动；拉高会进串口 ISP |

> **自制 DAPLink（立创"地阔星"类，STM32F103C8T6 小蓝板刷的）特别注意**：
> 它的 SWD 输出脚不是 `PA13/PA14`，而是 **`PB14`=SWDIO、`PB13`=SWCLK**（那两个是调试器自己被烧录用的口）。
> 板子丝印若印的是 `PB13/PB14`，按丝印接；别把调试器自身的 PA13/PA14 当成输出脚。
> 若板上有 `3V3` 输出且电流够，可以直接给 GD32 板供电（这样只需 4 根线）；否则 GD32 板必须独立供电。

接线自检顺序：**共地 → 3.3V 别接成 5V → SWDIO/SWCLK 没接反 → BOOT0=0**。
接好后不用急著烧，先跑阶段 5 第 1 步的探测任务。

#### 4.1.1 你这块板上的 10 针专用烧录口（大科电子 V2.0）

你补充的板卡信息：**大科电子 GD32F303VGT6 V2.0，带 10 针专用烧录口**。
好消息是 **SWD 链路现在已经是通的**（本机实测 `Cortex-M4 r0p1 detected`），
所以这一步**不需要为了「用满 10 针」重新接线**——照当前接法继续往下走即可。

「10 针」没有统一线序：ARM Cortex Debug 10 针、ST-LINK/V2 10 针、各家自定义排针
**针号相同但功能不同**，照网上编号表接可能把 3.3 V 灌到信号脚。
完整对照表、万用表取证步骤、必接/可选清单见 **[03 · 引脚表 1.2 节](03_pinout.md)**。

真正需要接的最多 6 根：`SWDIO`→PA13、`SWCLK`→PA14、`GND`（共地）、
`VTref/3V3`（电平检测）、可选 `nRESET`；其余 `SWO/TDI/TDO/KEY/NC` 一律不接。

> 还差三项信息才能把这块板的 10 针线序写死（**不影响现在烧录**）：
> ① 每个针旁边的**丝印**是什么（拍张照，或照着念一遍）；
> ② 排针**间距**是 1.27 mm 小座还是 2.54 mm 排针；
> ③ 这个口旁边有没有印 `SWD`/`JTAG`/`DAP`，是否同时引出 `TXD/RXD` / `BOOT0`。

> ⚠️ 实测 `0x08000000` 已有旧程序（`20000418 08000149 …`），**不是空片**。
> 阶段 5 烧 `factory.bin` 会覆盖它，属不可逆操作；我不会替你按烧录键，等你确认。

### 4.2 屏幕 ILI9341（2.8" SPI，KMRTM28028-SPI）

| 模块丝印 | GD32 | 说明 |
|---|---|---|
| `VCC` | `3.3V` | 不要接 5V（部分模块 5V 只供背光） |
| `GND` | `GND` | |
| `SCK` / `CLK` | `PA5`（SPI0_SCK） | |
| `SDI` / `MOSI` / `DIN` | `PA7`（SPI0_MOSI） | |
| `SDO` / `MISO` / `DOUT` | `PA6`（SPI0_MISO） | 屏幕可不接；触摸要用到 |
| `CS` | `PB0` | 低有效 |
| `DC` / `RS` / `D/C` | `PB1` | 0=命令，1=数据 |
| `RESET` / `RST` | `PC4` | 低有效；模块无 RST 可悬空 |
| `LED` / `BL` | `PC5` | 高电平亮 |

### 4.3 电阻触摸 XPT2046（与屏幕共用 SPI0，片选独立）

| 模块丝印 | GD32 | 说明 |
|---|---|---|
| `T_CLK` | `PA5` | 与 LCD SCK 共用 |
| `T_DIN` | `PA7` | 与 LCD MOSI 共用 |
| `T_DO` | `PA6` | 与 LCD MISO 共用 |
| `T_CS` | `PC6` | 独立片选，低有效 |
| `T_IRQ` / `PENIRQ` | `PC7` | 低有效，代码已启用内部上拉 |

> 触摸 IC 若不是 XPT2046（例如 GT911 / FT6236），驱动要改，先核对模块丝印。
> **排针顺序以你手上模块的丝印为准，不要只按线色**。改接线只需改 `config/board_config.h` 一个文件。

### 4.4 蜂鸣器 / ESP32-S3（可以后接）

| 外设 | GD32 | 说明 |
|---|---|---|
| 蜂鸣器控制 | `PB8` | **低电平响、高电平停**；有源蜂鸣器直接接，无源需 PWM |
| ESP32-S3 `TX` | `PA3`（USART1_RX） | 交叉 |
| ESP32-S3 `RX` | `PA2`（USART1_TX） | 交叉 |
| ESP32-S3 `GND` | `GND` | **必须共地** |

> **阶段 8 之前 ESP32-S3 完全可以不接**，先用 PC 串口验证 OTA，能少一半排障工作量。

设备管理器里记下 DAPLink 自带串口的 **COM 号**（本机是 `COM4`）。完整引脚表见 **[03 · 引脚表](03_pinout.md)**。

---

## 阶段 5 · 探测连接 → 烧录 ✅ 工具全部就位，不用装任何东西

> **这一阶段不需要安装任何软件**：编译器、OpenOCD、GDB 全在你电脑的 STM32CubeIDE 1.19.0 里，
> `config/toolchain_path.txt` 和 `config/openocd_path.txt` 已经替你指好路径。
> **不需要 Keil、不需要 GD32 插件、不需要 winget、不需要 Zadig**（DAPLink 走 HID，系统自带驱动）。

### 5.1 先探测，不擦不写（30 秒，确认线接对了）

`Ctrl+Shift+P` → `Tasks: Run Task` → **`17 · 探测 DAPLink/SWD 连接 (不擦不写)`**
（等价命令 `python tools/build.py probe`）。

期望看到这几行：

```
Info : CMSIS-DAP: FW Version = 2.2.0
Info : SWD DPIDR 0x2ba01477
Info : [stm32f1x.cpu] Cortex-M4 r0p1 processor detected
Info : [stm32f1x.cpu] Examination succeed
--- flash banks ---
```

✅ 出现 `Cortex-M4 r0p1` 和 `Examination succeed` = **线接对了，可以烧**。
❌ 报 `unable to find a matching CMSIS-DAP device` = DAPLink 没插好/换 USB 口/看设备管理器有没有 `USB 输入设备`；
❌ 报 `Error: ... DPIDR` 或一直 `polling` = 只查 SWDIO/SWCLK/GND 三根线和供电，`BOOT0` 保持 0。

> 探测命令用的是工程自带 **`config/openocd_gd32f303.cfg`**，里面做了两件关键修正：
> ① `set CPUTAPID 0x2ba01477`（GD32F303 是 Cortex-M4，官方 `stm32f1x.cfg` 默认按 M3 的
> `0x1ba01477` 校验，不改这句直接连不上）；② 锁定 DAPLink 的 HID 传输后端。
> 另外脚本会自动给 OpenOCD 加 `-s` 指到 CubeIDE 的脚本目录，省得你手动配 PATH。

### 5.2 烧录

`Ctrl+Shift+P` → `Tasks: Run Task` → **`10 · 烧录出厂镜像 (DAPLink/SWD)`**
（等价命令 `python tools/build.py flash-factory`）。

> 脚本会自动把 `build/factory.bin` 截成 **前 512 KiB** 再烧。原因：OpenOCD 的 `stm32f1x`
> 驱动对 GD32 容量探测不准，实测报 `flash size = 512 KiB`，直接写 1 MiB 会越界；
> 而 `factory.bin` 后半（Slot B）本来就是 `0xFF` 空片，不需要写。截前 512 KiB 完全等价且更快。

✅ **过关判据**：终端出现 `** Programming Finished **` / `** Verify Successful **` / `shutdown`，
然后串口 `COM4`（115200 8N1）出现：

```
GD32F303 Boot 1.0, A/B OTA
Jump slot A, v1.0, xxxxx bytes
GD32F303 App 1.0, slot A
Application slot A confirmed
```

屏幕点亮并显示主页面。

> 只烧单个分区（改代码后调试用）：任务 **`11 · 烧录 Boot`** / **`12 · 烧录 App-A`**。
> 注意单独烧 Boot 或 App 时元数据区不会被写，**建议用 `flash-factory` 整片初始化**。
>
> `factory.bin` 是 **1 MiB 整片镜像**（Boot + 预置有效元数据 + App-A），**只能用于首次生产 / 救砖**。
> 日常升级发 `build/packages/*.fw`，**绝不能把 `factory.bin` 当 OTA 包发送**。

### 5.3 换调试器 / 换电脑时怎么改

| 情况 | 改哪里 |
|---|---|
| 换成 ST-Link / J-Link | `python tools/build.py flash-factory --openocd-iface interface/stlink.cfg --openocd-target target/stm32f1x.cfg`（ST-Link 需 Zadig 装 WinUSB 驱动） |
| 换电脑、CubeIDE 版本不同 | 改 `config/openocd_path.txt`（openocd.exe 的 bin 目录）和 `config/toolchain_path.txt`；`.vscode/launch.json` 里 3 个绝对路径 |
| 用 winget / xPack 版 OpenOCD | 删掉 `config/openocd_path.txt` 内容即可，脚本会自动探测；或设环境变量 `OPENOCD_PATH` |
| 调试器是 CMSIS-DAP v2（WinUSB） | cfg 已默认 `cmsis-dap backend auto`，**不用改**；本版 OpenOCD 不支持 `bulk` 参数，别写它 |
| 报 `unable to find a matching CMSIS-DAP device` | 编辑该 cfg，取消注释 `cmsis-dap vid_pid 0x0d28 0x0204`（本机 Flash Pro v2） |

> **没有 SWD 调试器时**：可把 `BOOT0` 拉高，用 GD32 官方 ISP 工具经 `PA9/PA10` 串口烧，但配置繁琐，
> 建议还是备一个 DAPLink / ST-Link V2，十几块钱省半天排障。

---

## 阶段 6 · 在 VSCode 里看串口日志

1. 用一根 **TypeC 数据线**直插大科电子核心板——板载 USB 转串口芯片已接到 PA9/PA10，
   插上电脑就会多出一个 COM 口，**看日志不用接任何线**。
2. `Ctrl+Shift+P` → 输入 `Serial Monitor: Start Monitoring` → 选那个新 COM 口，
   波特率 `115200` → 8N1、无流控。
3. 给板子复位，应看到阶段 5 的那几行日志。

> 备选：DAPLink 自带 `COM4` 也能看日志，但需把 DAPLink 的 `TXD/RXD` 交叉接到 `PA10/PA9`——
> 而这两个脚已被板载串口芯片占用，**别把两路串口输出并联**，日志首选 TypeC。
> 板载串口的 COM 号以设备管理器「端口 (COM 和 LPT)」为准，
> 并把 `.vscode/tasks.json` 里 `comPort` 的默认值改成它。

看不到日志时按顺序查：用的是**数据线**不是纯充电线 → COM 号对不对 →
`BOOT0` 是否为 0 → 晶振宏 `HXTAL_VALUE=8000000` 是否与实际板子一致（8 MHz）。

---

## 阶段 7 · 断点调试（可选）

按 **F5**，选择 **`调试 Boot (OpenOCD, 会烧录)`**：会自动先编译 Boot，再通过 OpenOCD 烧录并停在 `main`。

> ⚠️ F5 的前提：**DAPLink 已插上电脑，并且接到板子的 10 针烧录口**。
> 报 `OpenOCD: GDB Server Quit Unexpectedly` 时先看 TERMINAL 页签：
> `unable to find a matching CMSIS-DAP device` = 调试器没插好/没接到板子；
> 不确定时先跑任务 **`17 · 探测 DAPLink/SWD`**（不擦不写）确认链路再 F5。
> 右下角 ESP-IDF 相关的构建报错与本工程无关，可忽略；彻底关掉见 [阶段 0.5](#阶段-05--禁用-esp-idf-扩展--打开工程后先做这一步)。

`.vscode/launch.json` 已替你配好 3 个绝对路径（CubeIDE 自带的 openocd.exe / st_scripts / toolchain bin），
并统一引用 `config/openocd_gd32f303.cfg`，**直接按 F5 即可，不用改配置**。

⚠️ **调试 App 必须用 `附加到 App-A (不复位 / 不擦 Flash)` 或 `附加到 App-B`**，
不要用 `launch` 方式——launch 会重新烧写并擦除扇区，可能破坏有效元数据导致回退逻辑误判。
正确顺序：先烧 `factory.bin` → 复位让 Boot 跳转 App → 再 F5 选 attach。

> 换电脑时改 `.vscode/launch.json` 里的 `serverpath` / `searchDir` / `armToolchainPath`。
> 没有 `.svd` 文件也能断点调试，只是看不到外设寄存器视图（GD32 的 SVD 需自己从兆易官网下载）。

---

## 阶段 8 · 第一次 OTA（先不用 Wi-Fi，最容易排障）

用 PC 串口就能完整验证 Boot 的擦写、CRC、试运行和回退，**ESP32-S3 可以晚一步再接**。

1. 编一个新版本：
   ```bash
   python tools/build.py --version 1.1 all
   ```
2. 让设备进入升级模式：在 App 界面点 **"进入升级模式"**（写升级标志 → 软复位回 Boot），
   或上电时 Boot 等待超时后自动进升级模式。屏幕应显示"等待固件升级 / 0%"。
> **DAPLink 自带串口 `COM4` 接的是 USART0（只输出日志）**，而 OTA 协议走 **USART1（PA2/PA3）**。
> 所以做串口 OTA 时**还需要第二路 USB-TTL**（任务 13 会问 `otaPort`，默认 `COM6`，按实际设备管理器填）。
> 接法：USB-TTL `TX`→`PA3`、`RX`→`PA2`、`GND` 共地。日志仍用 `COM4` 看。
> 想省掉第二路串口，可以跳到阶段 9 直接接 ESP32-S3 走 Wi-Fi OTA。

3. `Ctrl+Shift+P` → `Tasks: Run Task` → **`13 · 串口 OTA 升级 (B 槽)`**，
   按提示输入 OTA 串口号（`COM6`）和版本号（`1.1`）。

✅ **过关判据**：进度条 0→100% → **蜂鸣器短双音**（成功）→ 自动重启 →
App 显示 **v1.1 / slot B**。旧版 v1.0 / slot A 仍完整保留。

4. **测失败保护**（验收标准第 4 条）：在终端敲
   ```bash
   python tools/serial_ota.py --port COM6 --slot B \
       --package build/packages/app_b_v1.1.fw --yes --force-bad-crc
   ```
   应看到 **蜂鸣器长低音**、Boot 提示 APP 校验失败、旧固件仍可启动，且可以重新升级。

5. **测断电恢复与 A/B 回退**：详见 **[04 · 演示与压测](04_ota_demo.md)**。

---

## 阶段 9 · 接 ESP32-S3 做真正的 Wi-Fi OTA（可选，放最后）

1. VSCode 里跑任务 **`14 · 启动本地固件服务器`**（会持续运行，按 `Ctrl+C` 停），
   浏览器打开 `http://<PC 局域网 IP>:8000/manifest.json` 确认能下载。
2. 另装 **ESP-IDF v5.0+**（[Windows Installer](https://dl.espressif.com/dl/esp-idf/)），
   用 **ESP-IDF CMD** 终端：
   ```bash
   cd esp32_s3
   idf.py set-target esp32s3
   idf.py menuconfig      # "GD32 OTA Gateway": 填 Wi-Fi SSID/密码、manifest URL、UART 引脚
   idf.py build
   idf.py -p COMxx flash monitor
   ```
   > URL 里的 IP 必须是 **PC 在局域网里的地址**，不能写 `127.0.0.1`（那是 ESP32 自己）。
3. 接线与详细流程见 **[esp32_s3/README.md](../esp32_s3/README.md)** 和 **[04 文档](04_ota_demo.md)**。

---

## 卡住了？按这个表定位

| 现象 | 先查 |
|------|------|
| `shell 可执行文件…路径不存在` | 已修复：`.vscode` 不再硬编码 bash 路径；重开终端再跑任务 |
| 任务列表里找不到 `01`~`16` | 打开的不是 `gd32ota` 文件夹本身；或没点"信任" |
| `make: command not found` | 你在手动跑 `make`；改用 `python tools/build.py ...` |
| `python: 不是内部或外部命令` | 用 `py -3` 代替 `python`（把任务里的 `python` 统一换掉） |
| `arm-none-eabi-gcc: command not found` | 检查 `config/toolchain_path.txt` 里的路径是否还存在 |
| `lv_mbox: lv_btnm is required` | `config/lv_conf.h` 里 `LV_USE_MSGBOX` 必须为 0（已修） |
| `selected FPU does not support` | 缺 FPU 参数，脚本已带 `-mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| 编译报错一堆 `#error` | `GD32F30X_XD` 未生效，别手改构建参数 |
| 编译通过但 Boot 超 16 KiB | 见 [02 文档](02_build_and_flash.md) 第 6 节，去掉 printf 浮点 |
| 烧录后串口全乱码 | 共地 / TX-RX 交叉 / `HXTAL_VALUE` 晶振频率 |
| 上电停在"APP 无效" | 只烧了 Boot+App 没烧 `factory.bin` → 用任务 10 烧整片镜像 |
| 跳转后 HardFault | 检查 VTOR、`nvic_vector_table_set`、跳转前是否恢复全局中断 |
| 屏幕不亮 / 触摸不准 | 排针顺序与丝印核对；`TOUCH_RAW_*` 需实测校准（[03 文档](03_pinout.md)） |
| `unable to find a matching CMSIS-DAP device` | ① 调试器没插/换 USB 口；② `config/openocd_gd32f303.cfg` 取消注释 `cmsis-dap vid_pid 0x0d28 0x0204`；③ 该 cfg 已是 `backend auto`，**不要改成 `bulk`**（本版 OpenOCD 报 `invalid backend argument`） |
| `CMake Error: ... does not appear to contain CMakeLists.txt` | **与本工程无关**：ESP-IDF 扩展把 `gd32ota` 根目录当成 ESP-IDF 工程去构建了。别点 ESP-IDF 状态栏的构建/配置按钮；**照着 [阶段 0.5](#阶段-05--禁用-esp-idf-扩展--打开工程后先做这一步) 把 `espressif.esp-idf-extension` 选「禁用(工作区)」**。GD32 只走 `Ctrl+Shift+P → Tasks: Run Task` |
| `warning: ... has a LOAD segment with RWX permissions` | **不是错误**，裸机链接脚本把 FLASH 段标成 RWX 的正常现象。`build.py` / `Makefile` 已加 `-Wl,--no-warn-rwx-segments`，重新编译即不再出现 |
| `终端将被任务重用，按任意键关闭` | 只是 VSCode 的任务面板提示，不是报错。`.vscode/tasks.json` 里 17 个任务都已加 `"showReuseMessage": false`，以后不再打印、也不用按键 |
| `OpenOCD: GDB Server Quit Unexpectedly` | 看 TERMINAL 页签的真实报错：设备没插好 / cfg 路径错 / 端口被占。先跑任务 `17 · 探测` 确认链路，通了再 F5 |
| `expected 1 of 1: 0x1ba01477` | 用了裸 `target/stm32f1x.cfg`；必须用工程自带的 `config/openocd_gd32f303.cfg`（内含 `CPUTAPID 0x2ba01477`） |
| `Can't find interface/cmsis-dap.cfg` | OpenOCD 找不到脚本目录；`build.py` 已自动加 `-s`，别手敲原始 openocd 命令 |
| `STM32 flash size failed` / 写 1 MiB 越界 | 正常现象，GD32 容量探测不准；用任务 10 / `flash-factory`（自动截前 512 KiB） |
| `LIBUSB_ERROR_NOT_SUPPORTED` | ST-Link 才需要 Zadig 换 WinUSB；DAPLink 是 HID，不需要 |

---

## 完成度对照（你原始需求的验收标准）

| # | 验收标准 | 代码状态 | 需要你做的 |
|---|----------|----------|------------|
| 1 | 上电默认跳转现有 APP，界面正常 | ✅ 已实现 | 阶段 4~6 实板确认 |
| 2 | APP 触发升级后重启进 Boot，显示 Logo/版本号/进度 | ✅ 已实现 | 阶段 8 实板确认 |
| 3 | 串口/ESP32 收新固件，校验通过写入并运行新版本 | ✅ 已实现 | 阶段 8（串口）/ 阶段 9（Wi-Fi） |
| 4 | 校验失败蜂鸣器报错、保留旧固件、可重升 | ✅ 已实现 | 阶段 8 第 4 步 |
| 5 | 跳转后中断正常（VTOR 正确），日志无跑飞 | ✅ 已实现并修过 PRIMASK/MSP bug | 实板确认 |
| 6 | 两工程地址不重叠，Flash 不超容量 | ✅ 链接脚本 ASSERT + 22 项自测 + **实测链接占用** | 已确认 |
| — | 真 A/B 双槽、掉电不破坏旧固件、试运行失败自动回退 | ✅ 已实现（超出原需求的加固） | [04 文档](04_ota_demo.md) 压测 |

**结论：代码与工程 100% 完成、固件已编译出来、烧录工具链已全部就位并实测连通。**
按阶段 4 → 5 走完，就能看到屏幕点亮和串口日志。

### 本轮针对 DAPLink 新增/修改的文件

| 文件 | 作用 |
|---|---|
| `config/openocd_gd32f303.cfg` | **新建**。DAPLink(HID) + GD32F303 专用配置，含 `CPUTAPID 0x2ba01477` |
| `config/openocd_path.txt` | **新建**。指向 STM32CubeIDE 自带 OpenOCD 的 bin 目录 |
| `tools/build.py` | 自动定位 OpenOCD + 自动加 `-s` 脚本目录；新增 `probe` 命令；`flash-factory` 自动截前 512 KiB |
| `.vscode/tasks.json` | 新增任务 **`17 · 探测 DAPLink/SWD`**；任务 10 改为 DAPLink 专用；串口默认 `COM4` |
| `.vscode/launch.json` | 配好 CubeIDE 自带 openocd/gdb 绝对路径，统一引用新 cfg |
| `docs/06_vscode_guide.md` | 阶段 4/5/6/7 与排障表按 DAPLink 重写 |
| `docs/02_build_and_flash.md`、`docs/03_pinout.md` | 烧录章节与引脚表同步更新 |
