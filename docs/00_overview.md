# 00 · 项目总览

## 0. 硬件基线（板卡与调试器）

| 项 | 值 |
|---|---|
| MCU | GD32F303VGT6，LQFP100，Cortex-M4F，1 MiB Flash / 96 KiB SRAM |
| 目标板 | **大科电子 GD32F303VGT6 核心板**（丝印 Dake Elec F303 V1.0）。板载：10 针调试排针（SWD+UART）、板载 USB-UART（TypeC 即日志口）、1.8 寸 SPI TFT 屏座（TFT_CS=PD8/DC=PC4）、W25Q128 Flash（PA4）、KEY×3+NRST、LED×4；BOOT0 板上已偏置。详见 [03](03_pinout.md) §7 |
| 屏 / 触摸 | ILI9341 SPI（240×320）/ XPT2046 |
| 调试器 | **Flash Pro(CMSIS-DAP v2)** `VID_0D28:PID_0204`（FW 2.2.0），串口 `COM4`；`backend auto` 实测连通 |
| 工具链 | STM32CubeIDE 1.19.0 自带 `arm-none-eabi-gcc 13.3.1` + OpenOCD 0.12；**不需要 Keil / GD32 插件 / winget** |
| 已实测 | `probe` 通过：`DPIDR 0x2ba01477`、`Cortex-M4 r0p1`；22 项自测 OK；完整编译通过，6 个镜像已生成 |
| 只读采集 | `0x08000000` 已有旧程序、`0x08004000` 元数据区为空；**烧录会覆盖旧固件，须确认** |
| 待板测 | 烧录、屏幕点亮、触摸校准、串口 OTA、ESP32-S3 Wi-Fi OTA |

## 1. 目标与角色边界

```
┌──────────────┐   Wi-Fi / HTTP      ┌──────────────┐  UART(协议帧)  ┌───────────────────┐
│  固件服务器   │ ── manifest.json ──> │  ESP32-S3    │ ─────────────> │   GD32F303VGT6    │
│  (PC / 云)   │ ── app_X_vY.fw ───> │  Wi-Fi 网关   │ <── ACK/结果 ── │  Boot + App A/B   │
└──────────────┘                     └──────────────┘                └───────────────────┘
                                                                              │
                                                          ILI9341 SPI 屏 / XPT2046 / 蜂鸣器
```

**职责边界（很重要）**：

- ESP32-S3 **不写 GD32 的 Flash**，只做 Wi-Fi 下载 + 串口透传；Flash 擦写全部由 GD32 Bootloader 完成。
- Bootloader 是**裸机**，不引入 FreeRTOS/LVGL/业务传感器，屏幕用裸机 LCD 驱动绘制。
- App 跑 FreeRTOS + LVGL，两个任务：`GuiTask`（中优先级）与 `CommTask`（低优先级），
  外加一个只运行一次的 `Confirm` 任务做自检确认。
- PC 上的 `tools/serial_ota.py` 与 ESP32-S3 走**同一套串口协议**，因此可以先不碰 Wi-Fi，
  分层验证 GD32 侧的擦写/校验/回退逻辑。

## 2. 软件分层

```
Boot 业务 / App 业务
        ↑
   协议层 (common/protocol)     帧打包/解包、CRC16/CRC32、固件头、命令表
        ↑
   启动共享 (common/boot_shared) 分区表、双份元数据、启动策略、跳转
        ↑
   BSP (common/bsp)             时钟/串口/SPI/Flash/LCD/触摸/蜂鸣器
        ↑
   GD32F30x 标准库 + CMSIS + FreeRTOS + LVGL (third_party/)
```

## 3. Flash 分区（1 MiB，GD32F303VGT6）

| 区域 | 起始 | 结束(不含) | 大小 | 页大小 | 说明 |
|------|------|------------|------|--------|------|
| Bootloader | `0x08000000` | `0x08004000` | 16 KiB | 2 KiB | 裸机，链接 `linker/boot.ld` |
| 元数据页 A | `0x08004000` | `0x08004800` | 2 KiB | 2 KiB | 双份互为备份 |
| 元数据页 B | `0x08004800` | `0x08005000` | 2 KiB | 2 KiB | 写一份时另一份始终有效 |
| App Slot A | `0x08005000` | `0x08080000` | 492 KiB | 2 KiB (Bank0) | App-A 链接地址 |
| App Slot B | `0x08080000` | `0x08100000` | 512 KiB | 4 KiB (Bank1) | App-B 链接地址 |

- **不重叠、无空洞**：`0x4000 + 0x800 + 0x800 = 0x5000`，`0x5000 + 0x7B000 = 0x80000`，
  `0x80000 + 0x80000 = 0x100000`。主机侧测试 `tests/test_firmware.py` 会自动断言这些关系。
- GD32F303VGT6 是 XD 系列，**Bank0 页 2 KiB、Bank1 页 4 KiB**。
  `gd32f30x_fmc.c` 的 `fmc_page_erase()` / `fmc_word_program()` 会按地址自动选择 Bank，
  所以 `common/bsp/bsp_flash.c` 对两个槽用同一套 API 即可。
- 编译时**必须定义 `GD32F30X_XD`**（Makefile 已带，Keil 见 `keil/README.md`）。

## 4. A/B 双槽与掉电保护

原始需求写的是"Flash ≥512KB 预留双区备份思路"，本项目直接做成**真正的 A/B 双应用槽**：
同一套 App 源码编译两次，分别链接到 `0x08005000`（A）和 `0x08080000`（B）。

关键流程：

1. 升级时 Boot 通过 `boot_metadata_choose_download_slot()` 选**非当前活动槽**作为目标；
   握手应答把目标槽号告诉上位机，上位机（PC 工具 / ESP32）核对后才发对应 `.fw` 包。
2. 收固件头 → 先把目标槽元数据标记为 `INVALID` → 擦目标槽 → 分包写入。
   **旧槽从头到尾没被碰过**，此时断电最多是"新槽半截 + 旧槽完好"。
3. 全部收完后计算整包 CRC32，并检查向量表（MSP 落在 SRAM、8 字节对齐；
   Reset_Handler 带 Thumb 位且落在镜像范围内）。**只有全部通过**才把新槽置为 `PENDING`
   并写 `pending_slot`，然后复位进入试运行。
4. 试运行期间 `trial_count` 递增（上限 `BOOT_MAX_TRIALS = 3`）。
   App 起来后 `Confirm` 任务延时 5 s，确认调度器/串口/LCD/LVGL 都活着，
   再写 `CONFIRMED` 并清 `pending_slot`。
   **若新 App 反复复位没来得及确认，Boot 自动回退旧槽**并鸣失败音。
5. 元数据用两个独立 2 KiB 页交替写，每份带递增 `sequence` + CRC32，
   写一半断电时 Boot 仍能选出另一份有效副本。

### 槽状态

| 状态 | 含义 |
|------|------|
| `EMPTY` | 从未写入 |
| `CONFIRMED` | 已确认可正常启动 |
| `PENDING` | 新写入、等待试运行确认 |
| `INVALID` | 校验失败 / 试运行失败 / 写入中 |

### 蜂鸣器（低电平触发，PB8）

| 场景 | 提示 |
|------|------|
| 进入升级模式 | 短滴 50 ms |
| 升级校验成功 | 短双音（90 ms ×2） |
| 校验失败 / 回退 | 长低音 650 ms |

## 5. 固件头（40 字节）

```c
typedef struct {
    uint32_t magic;          // 0x4641574C ("FWL")
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t firmware_size;  // App 镜像字节数（不含 40 字节头）
    uint32_t crc32;          // CRC32/ISO-HDLC，与 zlib.crc32 一致
    uint32_t build_time;
    uint8_t  reserve[20];
} FirmwareHeader_t;          // packed，共 40 字节
```

**`.fw` 包 = 40 字节头 + 原始 App .bin**。App 本体仍从向量表（MSP / Reset_Handler）开始，
固件头只用于传输和校验，**不会被写进 Flash 的槽起始地址**，避免"固件头占了 Reset_Handler 位置"这类跳转错误。

## 6. 串口协议速览

```
| 帧头 2B | 命令 1B | 长度 2B | 数据 N B | CRC16 2B |
| 0xA5 0x5A | CMD   | LEN(LE) | PAYLOAD  | CRC(LE)  |
```
CRC16-CCITT-FALSE，初值 `0xFFFF`，多项式 `0x1021`，覆盖 `CMD + LEN + PAYLOAD`。
波特率 115200 8N1，单包数据 ≤ 256 B，每包带序号回 ACK，丢包/错校验回 NAK 重发。
完整命令表与状态机见 [05 · 协议与元数据](05_protocol.md)。

## 7. 三个镜像的关系

| 文件 | 大小 | 用途 |
|------|------|------|
| `bootloader.bin` | ≤16 KiB | SWD 单独烧 Boot |
| `app_a.bin` / `app_b.bin` | ≤槽容量 | SWD 单独烧某槽，或用于打包 |
| `factory.bin` | 固定 1 MiB | **首次生产/救砖**：Boot + 有效元数据 + App-A，整片烧到 `0x08000000` |
| `app_X_vY.fw` | 40 B + 镜像 | **日常 OTA 唯一发布物** |
| `manifest.json` | 小 JSON | ESP32-S3 读取的版本号与 A/B 包文件名 |

> 为什么需要 `factory.bin`：如果只 SWD 烧了 Boot + App-A 而元数据区是空白，
> Boot 会认为"没有有效 App"而停在升级界面。`factory_image.py` 会预置一份
> `CONFIRMED` 的 App-A 元数据（含正确 size/CRC）。
> App 自己也带"首次运行自登记"兜底，但用 factory.bin 最省事。

## 8. 可靠性设计对照

| 原始需求 | 实现 |
|----------|------|
| 长度范围 + CRC32 整包校验 | `boot_image_validate()` + `fwpack.py verify` |
| 校验失败不覆盖有效启动标志 | 先标记目标 `INVALID`，全部通过后才写 `pending_slot` |
| 断电/失败不破坏旧固件 | 只擦非活动槽；元数据双页交替 + sequence |
| 标志状态：正常/待升级/成功/失败 | `request_upgrade` + 4 种槽状态 + `trial_count` |
| 超时回跳旧 App | Boot 60 s 未收到数据，若旧槽有效则清标志并复位 |
| 跳转后中断正常 | `SCB->VTOR` = 槽地址；App main 再设一次 `nvic_vector_table_set`；**跳转前恢复全局中断** |
| 传输中定期喂看门狗 | 见下方"已知限制" |
| 多次升级/断电/校验失败压测 | [04 文档](04_ota_demo.md) 给出可复现步骤 |

## 9. 已知限制

1. **未在本机实编译**：当前机器没有 `arm-none-eabi-gcc` / `make` / Keil，
   固件只做了源码级与主机侧逻辑验证（`python tools/build.py test` 22 项全通过）。
   装好工具链后 `python tools/build.py all` 是第一次真链接；如遇报错见
   [01 文档第 5 节](01_environment_setup.md#5-第一次编译)。
2. **看门狗**：GD32 的 FWDGT 一旦使能无法用软件停止，所以参考工程**默认不开启**，
   跳转函数里也没有"停看门狗"。若你后续启用，App 必须接管喂狗，且 Boot 的升级循环里要定期喂。
3. **Boot 只有 16 KiB**：已用 `-Os` + `--gc-sections` + newlib-nano，
   且**没有**开 `-u_printf_float`。若你往 Boot 里加功能导致超限，`boot.ld` 的 `ASSERT` 会直接报错。
4. **屏幕/触摸需实板核对**：驱动 IC 已确认为 ILI9341 并按完整初始化表实现；
   但 KMRTM28028-SPI 模块的排针顺序、是否带 RESET/背光脚、触摸 IC 是否为 XPT2046
   仍需按丝印核对（[03 文档](03_pinout.md)）。`TOUCH_RAW_*` 校准值是占位默认值。
5. **ILI9341 颜色/方向**：默认 `MADCTL=0x48 (MX|BGR)`。若左右镜像或红蓝互换，
   改 `config/board_config.h` 的 `LCD_MADCTL` / `LCD_INVERT_COLORS`，
   **不要**改 `bsp_lcd.h` 的颜色宏（LVGL 的 `LV_COLOR_16_SWAP=0` 依赖它）。
6. **SPI 速率**：LCD 15 MHz、触摸约 0.94 MHz。若屏幕花屏先降 `LCD_SPI_PRESCALER`。
7. **A/B 固件不能混刷**：A 槽链接 `0x08005000`、B 槽链接 `0x08080000`，
   向量表里的绝对地址不同。PC 工具与 ESP32 都会先核对 Boot 返回的目标槽号再发包。
8. **串口协议没有加密/签名**：CRC 只防传输误码，不防恶意固件。
   量产请加签名验证、版本回滚保护与安全启动。
9. **115200 波特率**：约 500 KiB 固件纯传输需 40 s 以上，加上 Flash 擦写更久。
   可在 `board_config.h`、App main、ESP32 `menuconfig` 三处同步提高波特率。
10. **Bank1 页 4 KiB**：`bsp_flash_erase` 按页擦除，Slot B 起始 `0x08080000` 与 4 KiB 对齐；
    元数据页在 Bank0（2 KiB），三者不会跨页混淆。
11. **ESP32-S3 采用流式转发**：不缓存整包，靠 `.fw` 头里的 size/CRC 边下边发；
    若服务器在传输中长时间断流，`CONFIG_OTA_HTTP_TIMEOUT_MS` 会触发失败，
    GD32 侧保留旧固件并可重升。
12. **ESP-IDF 版本**：网关代码按 **v5.0+** 编写（用了 `uart_config_t.source_clk`、
    `esp_event_handler_instance_register` 等）。IDF 4.x 需小幅适配。
