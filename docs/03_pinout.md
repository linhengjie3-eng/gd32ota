# 03 · 引脚表与接线（引脚由本项目统一定义）

> **所有引脚集中在 `config/board_config.h`**。换接线只改这一个文件，不要改各 `.c`。
> 芯片：**GD32F303VGT6，LQFP100**，全部 3.3 V 逻辑电平。

## 1. 完整引脚表

| GD32 引脚 | 功能 | 方向 | 连接到 | 备注 |
|-----------|------|------|--------|------|
| `PA9` | USART0_TX | 出 | 板载 USB-UART（TypeC 即日志口）/ 10 针调试排针 | 调试日志，115200 8N1 |
| `PA10` | USART0_RX | 入 | 板载 USB-UART / 10 针调试排针 | 板上已连好，无需外部接线 |
| `PA2` | USART1_TX | 出 | ESP32-S3 **RX** | OTA 协议串口，交叉 |
| `PA3` | USART1_RX | 入 | ESP32-S3 **TX** | OTA 协议串口，交叉 |
| `PA5` | SPI0_SCK | 出 | LCD `SCK/CLK` + 触摸 `T_CLK` | 共用时钟 |
| `PA6` | SPI0_MISO | 入 | 触摸 `T_DO` | LCD 一般只写，MISO 可只接触摸 |
| `PA7` | SPI0_MOSI | 出 | LCD `SDI/MOSI` + 触摸 `T_DIN` | 共用数据线 |
| `PB0` | GPIO 输出 | 出 | LCD `CS` | 低有效 |
| `PB1` | GPIO 输出 | 出 | LCD `D/C` (`RS`) | 0=命令，1=数据 |
| `PC4` | GPIO 输出 | 出 | LCD `RESET` | 低有效；模块无 RST 脚可悬空 |
| `PC5` | GPIO 输出 | 出 | LCD `LED/BL` | 高电平亮背光 |
| `PC6` | GPIO 输出 | 出 | 触摸 `T_CS` | 低有效 |
| `PC7` | GPIO 输入 | 入 | 触摸 `PENIRQ` | 低有效，已用 GD32 内部上拉 |
| `PB8` | GPIO 输出 | 出 | 蜂鸣器控制 | **0 响 / 1 停**（低电平触发） |
| `PA13` | SWDIO | — | DAPLink `SWDIO` | **严禁复用** |
| `PA14` | SWCLK | — | DAPLink `SWCLK` | **严禁复用** |
| `NRST` | 复位 | — | DAPLink `RST`（可选） | 不接也能烧，工程用软复位 |
| `BOOT0` | 启动选择 | — | 保持 0 | 从主 Flash 启动 |
| `GND` | 地 | — | 所有模块共地 | **必须共地**，否则串口乱码 |
| `3.3V` | 电源 | — | 各模块 VCC | **不要接 5V 到任何信号脚** |

### 对应宏（`config/board_config.h`）

```c
DEBUG_UART_*    USART0 / GPIOA / PA9,PA10
ESP_UART_*      USART1 / GPIOA / PA2,PA3
BOARD_SPI_*     SPI0   / GPIOA / PA5,PA6,PA7
LCD_CS/DC/RST/BL   GPIOB/PB0, GPIOB/PB1, GPIOC/PC4, GPIOC/PC5
TOUCH_CS/IRQ       GPIOC/PC6, GPIOC/PC7
BUZZER_*           GPIOB/PB8（BUZZER_ACTIVE_LOW=1）
```

### 1.1 DAPLink 接线（本机实际用的调试器）

本机调试器是 **Flash Pro(CMSIS-DAP v2)**，`VID_0D28:PID_0204`，自带串口 `COM4`，
`config/openocd_gd32f303.cfg` 用 `backend auto`（实测 hid / auto 都能连上芯片）。
**日志走板载 TypeC 的 USB-UART，不需要接 TXD/RXD，也不需要 Zadig 装驱动**。
以板上 10 针排针的丝印/网络标号为准（`SWDIO`/`SWCLK`/`3V3`/`GND`）：

| DAPLink 丝印 | GD32F303VGT6 | 说明 |
|---|---|---|
| `SWDIO`（`DIO`/`DAT`） | `PA13` | 严禁复用 |
| `SWCLK`（`CLK`） | `PA14` | 严禁复用 |
| `GND` | `GND` | **必须共地** |
| `3V3` | `3.3V` | 绝不能接 5V；板子已独立供电时可不接 |
| `TXD` | `PA10` | 可不接：板载 USB-UART（TypeC）已连 PA9/PA10；要用 DAPLink 的 COM4 看日志才接，交叉，115200 8N1 |
| `RXD` | `PA9` | 同上（交叉） |
| `RST`（若有） | `NRST` | 可选，不接也能烧 |
| `BOOT0` | 保持 0 | 拉高会进串口 ISP |

> ⚠️ **自制 DAPLink（立创「地阔星」类，STM32F103C8T6 小蓝板刷的）**：
> 它的 SWD **输出脚是 `PB14`=SWDIO、`PB13`=SWCLK**，板上的 `PA13/PA14` 是调试器自身被烧录用的口，
> 别接错。板子印了什么就按什么接。
>
> 烧录前先 `python tools/build.py probe` 探测（不擦不写），看到 `Cortex-M4 r0p1 detected` 再烧。
> 配置见 `config/openocd_gd32f303.cfg`，完整步骤见 [06 文档](06_vscode_guide.md) 阶段 4~5。

### 1.2 板上 10 针专用烧录口（大科电子 GD32F303VGT6 V2.0）

**板卡基线**：目标板 = **大科电子 GD32F303VGT6 V2.0**，板上引出一个 **2×5 / 10 针专用烧录口**。
芯片级事实：GD32F30x 用户手册 Debug(DBG) 章节规定 SWD 只有两根信号线，
**PA13 = SWDIO、PA14 = SWCLK**，与本项目引脚表一致；本机实测 `DPIDR 0x2ba01477`、`Cortex-M4 r0p1` 已印证。

> ⚠️ **「10 针」不等于「标准 ARM 10 针」，线序不能靠数针脚猜。**
> 同样是 10 针，公开资料里至少有三种互不兼容的排列，针号相同而功能不同：

| 常见 10 针定义 | 1 | 2 | 3 | 4 | 5 | 6 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|
| ARM Cortex Debug（1.27 mm 2×5） | VTref | SWDIO | GND | SWCLK | GND | SWO | GNDDetect | nRESET |
| ST-LINK/V2 独立版（2.54 mm） | 3.3V | NRST | GND | SWDIO | GND | SWCLK | GND | TDI |
| 部分 JLink/CMSIS-DAP 资料的 2×5 变体 | VTref | SWDIO | GND | SWCLK | nRESET | GND | NC | NC |

（ARM 标准第 7 针是防呆缺针 KEY、第 8 针常为 NC/TDI；不同资料对 5/6/9/10 的写法也不统一。
第 1 行与第 3 行针号完全一样，但 5/6 脚一个是 GND/SWO、一个是 nRESET/GND —— 照错表接
可能把 3.3 V 或复位信号灌到 SWD 数据脚。）

**结论：10 针里真正需要接的最多 6 根，其余是 GND、NC 和防呆位。**

| 必要性 | 信号 | 说明 |
|---|---|---|
| 必接 | `SWDIO` → PA13 | 双向数据 |
| 必接 | `SWCLK` → PA14 | 时钟 |
| 必接 | `GND` | 必须共地；多个 GND 针建议都接 |
| 建议 | `VTref` / `3V3` | 调试器据此匹配电平；**只做检测，别从这里反向给板子供电** |
| 可选 | `nRESET` / `NRST` | 工程用 `program ... reset`（软复位），不接也能烧 |
| 不接 | `SWO`、`TDI`、`TDO`、`KEY/NC` | 纯 SWD 用不到 |

**不猜线序的取证顺序（断电测，万用表即可）**：

1. 找 **pin 1** 标记：丝印圆点 / 方框 / 缺口，先确认排针方向。
2. **断电**，通断档找出所有与板子 GND 连通的针 → 记为 GND。
3. **上电**，电压档找出 3.3 V 的针 → 记为 VTref/VCC；若量到 5 V，立刻停用该针。
4. 剩下两根按**板子丝印**（`SWDIO`/`SWCLK` 或 `DIO`/`CLK`）接，不要按上面任何一张表套针号。
5. 接完先 `python tools/build.py probe`（不擦不写），看到 `Cortex-M4 r0p1 detected` 才算通。

**本机实测证据（2026-11-19，只读采集，未擦未写）**：
`CMSIS-DAP FW 2.2.0`、`SWD DPIDR 0x2ba01477`、`Cortex-M4 r0p1 processor detected`、`Examination succeed`；
`mdw 0x08000000` = `20000418 08000149 08000209 08000199`（**已有出厂/旧程序，非空片**），
`mdw 0x08004000` = 全 `0xFFFFFFFF`（本项目元数据区为空）。
也就是说 **SWD 链路现在已经通了，不必为了「用满 10 针」重新接线**；
后续烧 `factory.bin` 会覆盖现有固件，属不可逆操作，须显式确认后再执行。

> **待确认（不阻塞烧录）**：这块 V2.0 板 10 针的**具体线序丝印**、排针间距（1.27 mm 还是 2.54 mm）、
> 该口是否同时引出 `TXD/RXD` / `BOOT0`。拿到丝印或原理图后补进本节；在此之前按上面「按丝印取证」执行。

## 2. ILI9341 屏幕接线（2.8" SPI，240×320）

典型 KMRTM28028-SPI 模块排针（**以你手上模块丝印为准**）：

| 模块丝印 | 接到 GD32 | 说明 |
|----------|-----------|------|
| `VCC` | 3.3V | 部分模块可 5V 供电，但信号必须 3.3V |
| `GND` | GND | |
| `CS` | `PB0` | |
| `RESET` / `RST` | `PC4` | 没有该脚时接到 3.3V，代码里的硬件复位会变成空操作，仍有软件复位 |
| `DC` / `RS` / `D/C` | `PB1` | |
| `SDI` / `MOSI` / `SDA` | `PA7` | |
| `SCK` / `CLK` / `SCL` | `PA5` | |
| `LED` / `BL` / `BLK` | `PC5` | 高亮；若为低电平亮，把 `LCD_BL_ACTIVE_HIGH` 改 0 |
| `SDO` / `MISO` | 可不接 | ILI9341 本工程只读不写显存 |

> ⚠️ **SPI 屏和触摸共用同一组 SPI0（PA5/PA6/PA7），靠不同片选区分**。
> `bsp_spi_lcd_mode()` 用 15 MHz，`bsp_spi_touch_mode()` 降到约 0.94 MHz（XPT2046 要求），
> 切换时会重新初始化 SPI0 的分频。

### 驱动实现要点（`common/bsp/bsp_lcd.c`）

- 上电先**硬件复位**（PC4 拉低 20 ms → 拉高 120 ms），再发 `0x01 SWRESET`。
- 走**完整 ILI9341 初始化表**：电源控制 `0xCF/0xED/0xE8/0xCB/0xF7/0xEA`、
  `0xC0/0xC1/0xC5/0xC7`、`0x36 MADCTL`、`0x3A PIXFMT=0x55`、
  `0xB1/0xB6/0xF2/0x26`、正负伽马 `0xE0/0xE1`、`0x11 SLPOUT` + 120 ms、`0x29 DISPON`。
  仅发 SWRESET+SLPOUT+DISPON 往往点不亮或颜色异常。
- `0x2A` 列地址 0..239，`0x2B` 页地址 0..319，`0x2C` 写 RGB565 像素流。
- 数据始终是 **RGB565**，所以 LVGL 的 `LV_COLOR_16_SWAP` 必须保持 **0**。

### 显示异常时的调整（只改 `config/board_config.h`）

| 现象 | 改哪里 |
|------|--------|
| 左右镜像 | `LCD_MADCTL` 去掉 `MX`：`0x48` → `0x08` |
| 上下颠倒 | `LCD_MADCTL` 加 `MY`（`|0x80`） |
| 横竖颠倒（变成 320×240） | 需要改 `LCD_WIDTH/HEIGHT` 与 MADCTL 的 `MV`，并同步 LVGL 分辨率 |
| 红蓝互换 | 去掉 `BGR` 位：`0x48` → `0x40`；**不要**改 `bsp_lcd.h` 的颜色宏 |
| 画面呈负片 | `LCD_INVERT_COLORS` 改成 `1`（发 `0x21 INVON`） |
| 整体偏移 / 边缘缺线 | 调整 `LCD_X_OFFSET` / `LCD_Y_OFFSET` |
| 花屏、乱码 | 把 `LCD_SPI_PRESCALER` 从 `SPI_PSC_8` 调到 `SPI_PSC_16` 或 `SPI_PSC_32` |

## 3. 触摸（XPT2046 / TSC2046）

| 模块丝印 | 接到 GD32 |
|----------|-----------|
| `T_CLK` | `PA5` |
| `T_CS` | `PC6` |
| `T_DIN` | `PA7` |
| `T_DO` | `PA6` |
| `T_IRQ` / `PENIRQ` | `PC7`（低有效，GD32 内部上拉） |

代码用 `0xD0` 读 X、`0x90` 读 Y，各采样 5 次取平均，再按 `TOUCH_RAW_*` 线性映射到屏幕坐标。

### 触摸校准（首次上板必做）

`config/board_config.h` 里的默认值是占位：

```c
#define TOUCH_RAW_X_MIN  200
#define TOUCH_RAW_X_MAX  3900
#define TOUCH_RAW_Y_MIN  200
#define TOUCH_RAW_Y_MAX  3900
#define TOUCH_SWAP_XY    0
#define TOUCH_INVERT_X   0
#define TOUCH_INVERT_Y   0
```

校准步骤：

1. 在 `app/src/lv_port_indev.c` 的 `touch_read()` 里临时加一句打印原始值：
   ```c
   printf("raw x=%u y=%u pressed=%d\r\n", raw_x, raw_y, pressed);
   ```
   （或临时把 `bsp_touch_read()` 的 `raw_x/raw_y` 打出来。）
2. 打开串口，分别点屏幕**左上角**和**右下角**，记录两组原始值。
3. 把 X 的最小值/最大值填 `TOUCH_RAW_X_MIN/MAX`，Y 同理。
4. 若点击位置与实际左右相反 → `TOUCH_INVERT_X=1`；上下相反 → `TOUCH_INVERT_Y=1`；
   X/Y 整体对调 → `TOUCH_SWAP_XY=1`。
5. 重新 `python tools/build.py app_a`、`python tools/build.py app_b`，再 `python tools/build.py packages`，进 App 点按钮验证。

> 若你的模块触摸 IC 不是 XPT2046（例如 GT911、FT6236、STMPE610），
> 只需重写 `common/bsp/bsp_touch.c`，`lv_port_indev.c` 与 App 不用改。

## 4. ESP32-S3 接线

| ESP32-S3 | 接到 GD32 | 说明 |
|----------|-----------|------|
| `GPIO17`（默认 TX） | `PA3` (USART1_RX) | 在 `idf.py menuconfig` 里可改 |
| `GPIO18`（默认 RX） | `PA2` (USART1_TX) | 交叉 |
| `GND` | `GND` | **必须共地** |
| `3.3V` | 独立供电 | 不要从 GD32 板取大电流 |

- 电平都是 3.3 V，不需要电平转换。
- 波特率必须与 GD32 一致（默认 115200）；若提高，三处同步改：
  `config/board_config.h` 的 `BOARD_ESP_UART_BAUD`、
  `boot/src/main.c` 的 `ESP_BAUDRATE`、
  `app/src/main.c` 的 `bsp_esp_uart_init(115200)`、
  ESP32 的 `CONFIG_OTA_UART_BAUD`。
- Wi-Fi 必须是 **2.4 GHz**（ESP32-S3 不支持 5 GHz）。

## 5. 蜂鸣器

- 接 `PB8`，**低电平触发**：`gpio_bit_reset()` 响，`gpio_bit_set()` 停。
- 若你的蜂鸣器模块是高电平触发，把 `BUZZER_ACTIVE_LOW` 改成 `0`，
  `bsp_board.c` 里已有 `#if` 分支自动反转。
- 有源蜂鸣器直接接 GPIO 即可；无源蜂鸣器需要 PWM，当前代码按有源处理。
- 提示音定义：成功短双音（90 ms ×2）、失败长低音（650 ms）、进入升级模式短滴（50 ms）。

## 6. 上电前核对清单

- [ ] 屏幕接法先定：**用板载 1.8 寸屏座还是外接 ILI9341 模块**（见 §7 待确认项）
- [ ] 模块排针丝印与上表逐脚对应，特别是 `DC/RS` 与 `RESET` 别接反
- [ ] 所有模块 **GND 共地**（GD32、屏、触摸、ESP32-S3、调试器）
- [ ] 所有信号线 3.3 V，没有 5 V 串入
- [ ] `PA13/PA14` 只接 SWD；`BOOT0` 板上已由 R7/R8 偏置，无需跳线
- [ ] 串口分工：USART0(PA9/PA10) 已被板载 USB-UART + 10 针排针占用（日志）；USART1(PA2/PA3) 留给 ESP32
- [ ] `config/board_config.h` 中引脚宏与实际接线一致
- [ ] 首次先不接 ESP32-S3，用板载 USB-UART（TypeC）看日志验证 Boot（见 [04 文档](04_ota_demo.md)）

## 7. 本板适配：大科电子 GD32F303VGT6 核心板（丝印 Dake Elec F303 V1.0）

依据你提供的核心板原理图与实物照片（2026-11-19 读图确认）：

| 板载资源 | 原理图网络 | MCU 引脚 | 对工程的影响 |
|---|---|---|---|
| **10 针调试排针（2×5）** | `SWDIO_PA13` `SWCLK_PA14` `UART1_TX_PA9` `UART1_RX_PA10` `3V3` `GND` | PA13/PA14/PA9/PA10 | **烧录口 = SWD = PA13/PA14**；与工程一致 |
| **板载 USB-UART**（黄色排针 `TX RX GND 3V1 5V0` + TypeC） | 串口 → PA9/PA10 | PA9/PA10 | **日志只需一根 TypeC 线**，零接线 |
| 板载 1.8 寸 SPI TFT 屏座（P2） | `SPT_SCK/SPT_MOSI/SPT_MISO/TFT_CS/TFT_DC/TFT_RST/BL`；背光 LED1-3 经 R13/14/15 | **TFT_CS=PD8、TFT_DC=PC4**（底部网络表） | **与工程外接假设（CS=PB0、DC=PB1、RST=PC4）不一致**，用屏座必须改 `board_config.h` |
| 板载 W25Q128JVSIQ SPI Flash | `F_CS=PA4`（+SCL/SDI/SDO） | PA4 | 工程未用 PA4，无冲突 |
| 按键 KEY1/KEY2/KEY3 + NRST | 经扩展排针 J3 | — | App 界面按键可改绑板载按键 |
| LED LD1-LD4 | 扩展排针 | — | 可做运行指示 |
| BOOT0 / BOOT1 | R7/R8 10K 偏置 | — | 板上固定，**无需 BOOT0 跳线** |

工程用到引脚与扩展排针（J2/J3）的对应关系（排针上有丝印，以丝印为准）：

| 工程引脚 | 功能 | 扩展排针位置（读图所得） |
|---|---|---|
| `PA2/PA3` | ESP32 串口 USART1 | J2 左列 13 / 右列 31 |
| `PA5/PA6/PA7` | SPI0 SCK/MISO/MOSI | J2 右列 30 / 左列 15 / 右列 29 |
| `PB0/PB1` | 外接屏 CS/DC | J2 左列 17 / 右列 27 |
| `PC4/PC5` | 外接屏 RST/BL | J2 左列 16 / 右列 28 |
| `PC6/PC7` | 触摸 CS/IRQ | J3 左列 10 / 右列 36 |
| `PB8` | 蜂鸣器 | J2 左列 5 |
| `PA13/PA14` | SWD | 板上 10 针调试排针 |
| `PA9/PA10` | 日志串口 | 板载 USB-UART + 10 针排针（已连好） |

**所以「引脚没有」是误会**：工程用到的每一个引脚都在 J2/J3 排针上。真正的差异是——
这块板的**板载屏座**用的引脚（PD8/PC4）和工程默认的外接屏引脚不同。

### 接线分区（就按你说的接，工程配置一行都不用改）

| 区域 | 接什么 | 用哪组排针 |
|---|---|---|
| **板上专门 10 针烧录区**（左上，2×5） | **只接 DAPLink**：`SWDIO` / `SWCLK` / `GND` / `3V3` | 专用烧录区，**整排卡着接 / 排线对插**；不要从 J2/J3 引 PA13/PA14 |
| **J2 / J3 扩展 IO 排针** | 屏幕 ILI9341、触摸 XPT2046、蜂鸣器、ESP32-S3 | 工程 `board_config.h` 里**全部**功能引脚都在这里 |
| **TypeC** | 日志（板载 USB-UART，已接 PA9/PA10） | 不用接线 |

> 你之前说「配的引脚没有」是对的：**`PA13/PA14` 在 J2/J3 上确实没引出**，它们只在专门烧录区。
> 不用去扩展排针上找，也不用拿杜邦线从别处引——DAPLink 直接卡到烧录区就行。
> 屏幕/触摸/蜂鸣器全走 J2/J3，工程默认引脚（PB0/PB1/PC4/PC5/PC6/PC7/PB8、PA2/PA3、PA5-PA7）
> 与 J2/J3 一一对应（见上表），**`config/board_config.h` 不用改一行**。
> 板载 1.8 寸屏座是另一套引脚（PD8/PC4），你不插它就完全不冲突。

> ⚠️ **「卡着接」的唯一前提**：DAPLink 那头 10 针的针序与板子烧录区一致。
> 两边都是标准 ARM Cortex Debug 10pin（1.27 mm 2×5）才能直接用 10 芯排线对插；
> 针序不一致时排线直连可能把 3V3 灌到信号脚。**断电用万用表先确认两边 GND / 3V3 的位置**。
> 本机实测（2026-11-19）：当前这套连接已能读到芯片（`DPIDR 0x2ba01477`、`Cortex-M4 r0p1`），
> 说明你现在这么接是通的，别再拆了重接。

### 待确认（必须先确认再改配置，不猜）

1. **10 针调试排针的精确针序**（10 针各是什么信号）——接 DAPLink 需要；接错可能烧调试器或板子。
2. **屏座 `SPT_SCK/SPT_MOSI/SPT_MISO` 走哪路 SPI**：原理图上 SPI1 组（PB12/PB13/PB15）与 SPI0 组（PA6/PA7）都在，截图读不出屏座实际连接。
3. **`TFT_RST`、`BL` 接到哪些引脚**。
4. **屏驱动 IC**：1.8 寸 128×160 常见是 ST7735，**不是 ILI9341**；若外接 ILI9341 模块则工程初始化表可直接用。
5. 1.8 寸屏基本无触摸；要触摸需外接 XPT2046 模块到 PC6/PC7。

> 以上确认后，改 `config/board_config.h`（必要时含 `bsp_spi.c` 的 SPI 外设号）即可适配屏座。
> **烧录与看日志不依赖屏幕**——先把调试器和 TypeC 接好、跑通烧录，屏幕适配随后改。
