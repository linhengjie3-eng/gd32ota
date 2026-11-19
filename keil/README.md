# Keil MDK 工程配置说明

> **推荐路线是 GCC**（`python tools/build.py all` 免 make；或根目录 `Makefile` 一条 `make all` 出齐所有镜像）。
> Keil 路线适合你已有 MDK-ARM 许可证、或公司流程要求用 Keil 的场景。
> 两条路线产出的是**同一套源码**，只是链接器和启动文件不同。

---

## 1. 安装

1. 安装 **Keil MDK-ARM 5.36 以上**（含 ARM Compiler 5 或 6）。
2. 安装 **GigaDevice GD32F30x DFP**：
   - 打开 Keil 的 *Pack Installer* → 搜索 `GD32F30x` → Install；
   - 或去 <https://www.gd32mcu.com/cn/download> 下载 `GigaDevice.GD32F30x_DFP.x.x.x.pack` 双击安装。
3. 确认 *Project → Manage → Pack Installer* 里能看到 `GD32F303VG`。

## 2. 建两个（或三个）独立工程

| 工程 | 用途 | 链接地址 | 大小 |
|------|------|----------|------|
| `Boot` | 裸机 Bootloader | `0x08000000` | `0x4000` (16 KiB) |
| `App_A` | FreeRTOS+LVGL，A 槽 | `0x08005000` | `0x7B000` (492 KiB) |
| `App_B` | 同一套 App 源码，B 槽 | `0x08080000` | `0x80000` (512 KiB) |

`App_B` 只是把 `App_A` 复制一份并改宏，**源码完全相同**。日常只发布与 Boot 返回的目标槽匹配的那个 `.fw` 包。

## 3. Options for Target 逐项设置

### Device
选 `GD32F303VG`（对应 VGT6，LQFP100，1 MiB Flash / 96 KiB SRAM）。

### Target 页
- 勾选 **Use MicroLIB**（`printf` 走 `fputc` 重定向，见 `common/bsp/syscalls.c` 的 `#if !defined(__GNUC__)` 分支）。
- **不勾选** "Use Memory Layout from Target Dialog"，改用下面的 Scatter File：
  - Boot → `keil/boot.sct`
  - App_A → `keil/app_a.sct`
  - App_B → `keil/app_b.sct`
- 若不用 scatter，等价的 IROM1/IRAM1 填法：

| 工程 | IROM1 Start | IROM1 Size | IRAM1 Start | IRAM1 Size |
|------|-------------|------------|-------------|------------|
| Boot | `0x08000000` | `0x4000` | `0x20000000` | `0x18000` |
| App_A | `0x08005000` | `0x7B000` | `0x20000000` | `0x18000` |
| App_B | `0x08080000` | `0x80000` | `0x20000000` | `0x18000` |

### C/C++ (AC6) 页 — Preprocessor Symbols

```
USE_STDPERIPH_DRIVER, GD32F30X_XD, HXTAL_VALUE=8000000
```

- `GD32F30X_XD` **必须定义**：1 MiB Flash 属于 XD 系列，否则 `gd32f30x.h` 直接 `#error`，且中断向量表/FMC 容量会按 HD 处理。
- `HXTAL_VALUE=8000000`：板上 8 MHz 晶振，PLL 才能算出 120 MHz。
- App_B 额外加：`APP_SLOT_B`
- 版本号（可选覆盖默认 1.0）：`APP_VERSION_MAJOR=1, APP_VERSION_MINOR=1`
- App_A / App_B **不要**定义 `BOOT_BUILD`。

### C/C++ 页 — Include Paths

```
config
common\bsp
common\protocol
common\boot_shared
app\src                     (Boot 工程可省略)
boot\src                    (App 工程可省略)
third_party\gd32f30x\Firmware\CMSIS
third_party\gd32f30x\Firmware\CMSIS\GD\GD32F30x\Include
third_party\gd32f30x\Firmware\GD32F30x_standard_peripheral\Include
third_party\FreeRTOS-Kernel\include
third_party\FreeRTOS-Kernel\portable\GCC\ARM_CM4F
third_party\lvgl
```

> `config\gd32f30x_libopt.h` 是库配置头，`config\lv_conf.h` 是 LVGL 配置头，两者都靠 `config` 这个 include 路径被找到。
> Keil 不需要 `FreeRTOS-Kernel\portable\GCC\ARM_CM4F` 里的 `port.c`，但要 `portmacro.h`；
> Keil/AC6 请使用 `third_party\FreeRTOS-Kernel\portable\RVDS\ARM_CM4F\port.c`。

### 源文件分组

**Boot 工程**
- `third_party/gd32f30x/Firmware/CMSIS/GD/GD32F30x/Source/ARM/startup_gd32f30x_xd.s`（注意是 **ARM** 目录，不是 GCC 目录）
- `third_party/gd32f30x/Firmware/CMSIS/GD/GD32F30x/Source/system_gd32f30x.c`
- `third_party/gd32f30x/Firmware/GD32F30x_standard_peripheral/Source/` 下全部 `.c`
- `boot/src/*.c`、`common/bsp/*.c`（`bsp_touch.c` 可不加）、`common/protocol/*.c`、`common/boot_shared/*.c`

**App 工程**（在 Boot 基础上）
- 去掉 `boot/src/*.c`
- 加 `app/src/*.c`、`common/bsp/bsp_touch.c`
- 加 FreeRTOS：`tasks.c` `queue.c` `list.c` `portable/RVDS/ARM_CM4F/port.c` `portable/MemMang/heap_4.c`
- 加 `third_party/lvgl/src/**/*.c`（全部加入即可，未启用的模块由 `LV_USE_*` 条件编译掉）

### Linker 页
- Scatter File 指向上表对应 `.sct`。
- 生成 `.bin` 的 User 命令（After Build/Rebuild）：
  ```
  fromelf --bin --output=..\build\app_a.bin .\Objects\app_a.axf
  ```
  Boot 同理输出 `bootloader.bin`。

## 4. 三个必须注意的坑

1. **不要在 IROM1 里从 `0x08000000` 编译 App**。App 必须从 `0x08005000` 或 `0x08080000` 开始，且 `main()` 开头要执行
   `nvic_vector_table_set(NVIC_VECTTAB_FLASH, APP_VECTOR_OFFSET)`（工程已包含）。否则中断仍映射到 Boot 区，跳转后 HardFault。
2. **Boot 与 App 的地址不能重叠**，元数据区 `0x08004000..0x08005000` 也不属于任何 App。
   修改分区请同步改 `common/boot_shared/memory_map.h`、`.sct`、`tools/fwpack.py`、`tools/factory_image.py`。
3. **`__app_image_end` 符号**：GCC 下由 `linker/app.ld` 定义；Keil 下 `app/src/main.c` 已改用
   `Image$$ER_IROM1$$Limit`。首次上板请打开 `.map` 文件核对这个值等于实际镜像末尾；
   若不一致，App 的"首次运行自登记"会写入错误的 size/CRC。
   **最省事的做法**：用 `tools/factory_image.py` 生成的 `factory.bin` 整片烧录，
   元数据里已经写好正确的 size 与 CRC，App 无需自行计算。

## 5. 后续步骤

Keil 只负责产出 `.bin`。打包、manifest、整片镜像、串口升级、主机侧自测仍然用仓库里的 Python 工具：

```
python tools/fwpack.py pack --input build/app_a.bin --output build/packages/app_a_v1.1.fw --slot A --version 1.1 --manifest build/packages/manifest.json
python tools/factory_image.py --boot build/bootloader.bin --app-a build/app_a.bin --output build/factory.bin
python -m unittest discover -s tests -v
```

编译/烧录/演示的完整说明见 `docs/01_environment_setup.md` 与 `docs/04_ota_demo.md`。
