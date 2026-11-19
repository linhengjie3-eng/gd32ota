# GD32F303VGT6 双分区 IAP/OTA 升级终端

基于 **GD32F303VGT6（1 MiB Flash / 96 KiB SRAM，LQFP100）** 的 Bootloader + App 双工程固件升级终端。
Bootloader 负责上电自检、版本显示、串口接收固件、校验写入 Flash、跳转 App；App 跑 FreeRTOS + LVGL；
ESP32-S3 作为 Wi-Fi 网关下载固件并按协议分包转发；蜂鸣器给出成功/失败提示音。

> 屏幕驱动 IC 已确认：**ILI9341**（240×320 SPI）。触摸按 **XPT2046** 实现。
> **所有引脚由 `config/board_config.h` 统一定义**，改接线只改这一个文件。

---

## 快速开始（3 条命令）

```bash
python tools/build.py check       # 确认 arm-none-eabi-gcc 已装好
python tools/build.py test        # 22 项主机侧自测，不需要硬件
python tools/build.py all         # 编出全部镜像与 OTA 包
```

> **不需要 make，不需要 Git Bash**：`tools/build.py` 是纯 Python 构建入口，PowerShell / CMD / Git Bash 都能跑。
> 顶层 `Makefile` 完整保留，参数一致，供熟悉 Make / Linux / CI 的场景使用（需自行装 make + Git Bash）。

**还没搭开发环境？** → 用 VS Code 的话直接看 **[docs/06_vscode_guide.md](docs/06_vscode_guide.md)**（从打开文件夹开始的逐步教程）；
工具链安装的全部细节与替代方案在 **[docs/01_environment_setup.md](docs/01_environment_setup.md)**。
工程已自带 `.vscode/`：智能提示、**16 个一键任务**、`Ctrl+Shift+B` 编译、F5 调试、串口监视器都已配好，且不硬编码任何 shell 路径。

---

## 文档导航

| 文档 | 内容 |
|------|------|
| [06 · VS Code 一步步操作指南](docs/06_vscode_guide.md) | **从打开文件夹开始照着做**：装工具链 → 自测 → 编译 → 烧录 → OTA（推荐先读这篇） |
| [00 · 项目总览](docs/00_overview.md) | 架构、分区表、A/B 策略、可靠性设计、交付清单、已知限制 |
| [01 · 环境搭建](docs/01_environment_setup.md) | **从零装 GD32 开发环境**（免 make 路线 / Makefile 路线 / Keil 路线 / ESP-IDF） |
| [02 · 编译与烧录](docs/02_build_and_flash.md) | 构建产物、链接配置、SWD 烧录、出厂镜像 |
| [03 · 引脚表与接线](docs/03_pinout.md) | ILI9341 / XPT2046 / ESP32-S3 / 蜂鸣器 / SWD 引脚与触摸校准 |
| [04 · 演示与压测](docs/04_ota_demo.md) | 串口 OTA、Wi-Fi OTA、校验失败、断电恢复、A/B 回退 |
| [05 · 协议与元数据](docs/05_protocol.md) | 帧格式、命令表、状态机、双份元数据、试运行/回退规则 |
| [keil/README.md](keil/README.md) | Keil MDK 工程配置与三个 `.sct` 分散加载文件 |
| [esp32_s3/README.md](esp32_s3/README.md) | ESP32-S3 网关编译、menuconfig、接线 |

---

## 目录结构

```
gd32ota/
├── Makefile                 标准构建（需 make + Git Bash）
├── README.md
├── .vscode/                 VSCode：16 个任务 / 调试 / IntelliSense / 串口（不硬编码 shell 路径）
├── config/                  board_config.h(引脚) / lv_conf.h / FreeRTOSConfig.h / gd32f30x_libopt.h
├── linker/                  boot.ld / app.ld（A/B 地址由构建脚本预处理注入）
├── keil/                    boot.sct / app_a.sct / app_b.sct + Keil 配置说明
├── boot/src/                Bootloader 裸机主程序 + SysTick
├── app/src/                 FreeRTOS 任务、LVGL 页面、显示/触摸 port、升级入口
├── common/
│   ├── bsp/                 时钟/串口/SPI/Flash/LCD(ILI9341)/触摸(XPT2046)/蜂鸣器
│   ├── protocol/            帧打包解包、CRC16/CRC32、固件头、命令表
│   └── boot_shared/         分区表、元数据双页、启动策略、跳转 APP
├── esp32_s3/                ESP-IDF 工程（Wi-Fi 下载 + UART 分包转发）
├── tools/                   build.py(免 make 构建) / fwpack.py / serial_ota.py
│                            factory_image.py / firmware_server.py / fetch_lvgl_files.py
├── tests/                   test_firmware.py（主机侧 22 项自测）
├── docs/                    见上表
└── third_party/             GD32 标准库 / FreeRTOS-Kernel V10.5.1 / LVGL v8.3.11
```

---

## 分区表（1 MiB Flash）

| 区域 | 起始 | 大小 | 说明 |
|------|------|------|------|
| Bootloader | `0x08000000` | 16 KiB | 裸机，独立工程 |
| 元数据 A 页 | `0x08004000` | 2 KiB | 双份互为备份，带序号 + CRC32 |
| 元数据 B 页 | `0x08004800` | 2 KiB | 写一份时另一份始终有效 |
| **App Slot A** | `0x08005000` | 492 KiB | Bank0 区，页 2 KiB |
| **App Slot B** | `0x08080000` | 512 KiB | Bank1 区，页 4 KiB |

真正的 **A/B 双槽**：新固件永远写"当前未运行的那个槽"，旧固件在整包 CRC 校验通过前不会被擦除。

---

## 交付物对照（原始需求）

| 需求 | 交付位置 |
|------|----------|
| Bootloader + App 两个独立工程 | `boot/` `app/`，`Makefile` / `tools/build.py` / `keil/*.sct` |
| Keil 链接配置说明 | `keil/README.md` + 三个 `.sct` |
| BSP：串口/GPIO/Flash/LCD/触摸 | `common/bsp/`（LCD 为 ILI9341） |
| 协议层：帧打包/解包、CRC16/CRC32、命令处理 | `common/protocol/` |
| Boot 跳转、固件头校验、标志管理 | `common/boot_shared/` `boot/src/main.c` |
| APP 任务、LVGL 页面、升级入口 | `app/src/` |
| ESP32-S3 Wi-Fi 下载与串口转发 | `esp32_s3/` |
| 分区表/引脚表/编译下载步骤/演示流程/已知限制 | `docs/` |
| （额外）PC 串口升级工具、本地固件服务器、主机侧自测 | `tools/` `tests/` |
| （额外）VSCode 工程配置、免 make 构建脚本 | `.vscode/` `tools/build.py` |

---

## 状态与限制（务必先读）

- 本机**没有 ARM 交叉工具链**（也没有 `make` / OpenOCD），因此固件**未在本机实际编译链接**。
  已完成的是：全部源码、两套构建入口、依赖库齐备、主机侧 22 项自测全部通过、
  构建脚本 `--dry-run` 的 541 条命令展开已验证。
  你装好工具链后执行 `python tools/build.py all` 即为第一次真编译，若报错见
  [01 文档第 5 节](docs/01_environment_setup.md#5-第一次编译) 的速查表。
- 屏幕排针顺序、触摸 IC 型号请以你手上 KMRTM28028-SPI 模块的丝印为准；
  校准值 `TOUCH_RAW_*` 是占位默认值，需实测（见 [03 文档](docs/03_pinout.md)）。
- 独立看门狗（FWDGT）默认**不启用**：GD32 一旦使能无法软件停止，跳转函数里"停看门狗"做不到。
  若启用，App 必须接管喂狗。
- `.fw` 升级包头部是 40 字节，**App 本体仍从向量表开始**（固件头不占用 Reset_Handler 位置）。
- `factory.bin` 是整片 1 MiB 镜像，仅用于首次生产/救砖，**不能当 OTA 包发送**。
