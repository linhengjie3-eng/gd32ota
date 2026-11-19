# 05 · 串口协议、固件包与元数据

三方共用同一套定义，主机侧 `python tools/build.py test` 会断言 GD32 C 头、ESP32-S3 C 头、Python 工具的常量一致：

- `common/protocol/boot_protocol.h`（GD32）
- `esp32_s3/main/ota_protocol.h`（ESP32-S3）
- `tools/serial_ota.py` / `tools/fwpack.py`（PC）

## 1. 帧格式

```
偏移   长度   字段
0      2      帧头 0xA5 0x5A
2      1      CMD
3      2      LEN（小端，PAYLOAD 字节数，≤300）
5      N      PAYLOAD
5+N    2      CRC16（小端，覆盖 CMD + LEN + PAYLOAD）
```

- CRC16-CCITT-FALSE：初值 `0xFFFF`，多项式 `0x1021`，无输入/输出反转。
- 校验向量：`crc16("123456789") = 0x29B1`。
- 帧最小 7 字节（空 payload），最大 307 字节。
- 解析状态机支持粘包、垃圾前缀、`0xA5 0xA5 0x5A` 重同步、超长帧丢弃。

## 2. 命令表

| CMD | 名称 | 方向 | PAYLOAD |
|-----|------|------|---------|
| `0x01` | HANDSHAKE | ESP32/PC → GD32 | 空（查询就绪） |
| `0x02` | HANDSHAKE_RESP | GD32 → ESP32/PC | 12 B，见 §3.1 |
| `0x10` | FIRMWARE_HEADER | → GD32 | 40 B `FirmwareHeader_t` |
| `0x11` | FIRMWARE_DATA | → GD32 | 2 B 序号(LE) + ≤256 B 数据 |
| `0x12` | PACKET_ACK | GD32 → | 2 B 序号(LE) + 1 B 状态码 |
| `0x13` | FIRMWARE_END | → GD32 | 空（请求校验） |
| `0x14` | VERIFY_RESULT | GD32 → | 1 B 状态码 + 1 B 槽 + 2 B 主版本 + 2 B 次版本 |
| `0x20` | VERSION_QUERY | → GD32 | 空 |
| `0x21` | UPDATE_CHECK | App(GD32) → ESP32 | 空（App 点"检查更新"） |
| `0x22` | UPDATE_CHECK_RESP | ESP32 → App(GD32) | 1 B available + 2 B 主版本 + 2 B 次版本 |

### ACK 状态码（`0x12`）

| 值 | 含义 |
|----|------|
| 0 `ACK_OK` | 成功 |
| 1 `ACK_CRC_ERROR` | 帧 CRC 错（解析器已丢弃，通常不会回） |
| 2 `ACK_SEQUENCE_ERROR` | 序号不连续 |
| 3 `ACK_STATE_ERROR` | 当前状态不接受该命令 |
| 4 `ACK_FLASH_ERROR` | Flash 擦/写失败 |
| 5 `ACK_BUSY` | 忙 |
| 6 `ACK_LENGTH_ERROR` | 长度非法 |
| 7 `ACK_VERSION_ERROR` | **固件头版本低于当前已安装版本，拒绝降级**（见下） |

固件头帧的 ACK 使用特殊序号 **`0xFFFF`**（`PROTO_HEADER_ACK_SEQ`）。

#### 版本门槛（三道，各自拦不同的错误）

| 位置 | 判断 | 拦住什么 |
|------|------|----------|
| ESP32 网关 `version_is_newer()` | manifest 版本必须**严格高于**设备运行版本 | 不推同版本/低版本，打印 `already up to date, nothing to do` |
| ESP32 网关 `perform_ota()` 读 `.fw` 头后 | 包头版本必须 == manifest 版本，且严格高于运行版本 | manifest 改了但服务器仍放旧包（"标 1.3 发 1.0"） |
| PC `serial_ota.py` 握手后预检 | 包版本不得低于设备版本 | 省掉必然被拒的整包传输与擦槽 |
| GD32 Boot `handle_header()` 擦槽前 | 包版本不得低于已安装最高版本 | 防回滚攻击；也是唯一能拦住"绕过上位机直发串口"的一层 |

**同版本重刷是允许的**（`version_is_downgrade()` 只在严格更低时返回 true），
因为 `--force-bad-crc` 故障注入与 A/B 反复压测都需要重刷同一个版本。
需要 OTA 降级演示时用 `python tools/build.py --allow-downgrade boot` 重编 Boot，
并给 `serial_ota.py` 加 `--allow-downgrade`；**产品固件默认拒绝降级**。
回到最初版本请用 SWD 出厂烧录（`--version 1.0 flash-factory`），
它会重写元数据，不走 OTA 协议。

### VERIFY_RESULT 状态码（`0x14`）

| 值 | 含义 |
|----|------|
| 0 `VERIFY_OK` | CRC + 向量表通过，已写 `PENDING`，即将复位试运行 |
| 1 `VERIFY_CRC_FAILED` | 整包 CRC32 不符 |
| 2 `VERIFY_VECTOR_FAILED` | 向量表非法（MSP / Reset_Handler） |
| 3 `VERIFY_FLASH_FAILED` | 元数据写失败 |
| 4 `VERIFY_SIZE_FAILED` | 实际收到字节数 ≠ 头里的 `firmware_size` |

### 设备状态（握手应答 `payload[2]`）

| 值 | 含义 |
|----|------|
| 0 `DEVICE_STATE_APP` | 当前跑在 App，仅响应版本查询/检查更新 |
| 1 `DEVICE_STATE_BOOT_WAIT` | Boot 等待固件升级 |
| 2 `DEVICE_STATE_BOOT_RECEIVE` | Boot 正在接收分包 |
| 3 `DEVICE_STATE_BOOT_ERROR` | Boot 上一轮校验失败 |

## 3. PAYLOAD 具体布局

### 3.1 握手/版本应答（12 字节，Boot 与 App 都发 `0x02`）

```
[0..1]   PROTOCOL_VERSION (LE, 当前 0x0100)
[2]      device state
[3]      target_slot  (Boot 想写的槽 / App 下一次要写的槽)
[4..5]   当前活动 App 主版本 (LE)
[6..7]   当前活动 App 次版本 (LE)
[8..9]   Boot 主版本 (LE)
[10..11] Boot 次版本 (LE)
```

> 上位机**必须**用 `payload[3]` 决定发 `app_a_*.fw` 还是 `app_b_*.fw`。
> PC 工具会核对，不一致直接退出；ESP32-S3 用 manifest 里的 `packages.A/B` 选包。

### 3.2 固件头（40 字节）

```c
typedef struct {          // packed，小端
    uint32_t magic;          // 0x4641574C ("FWL")
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t firmware_size;  // App 镜像字节数，不含这 40 字节
    uint32_t crc32;          // CRC32/ISO-HDLC，与 zlib.crc32 相同
    uint32_t build_time;
    uint8_t  reserve[20];    // 全 0
} FirmwareHeader_t;
```

CRC32：初值 `0xFFFFFFFF`、多项式 `0xEDB88320`、末异或 `0xFFFFFFFF`。
校验向量 `crc32("123456789") = 0xCBF43926`。GD32 用逐位实现，PC 用 `zlib.crc32`，
`python tools/build.py test` 会断言两者逐字节一致。

### 3.3 数据包（`0x11`）

```
[0..1]   sequence (LE, 从 0 开始)
[2..]    image bytes
```
- 每包数据固定 256 B，**只有最后一包可以短于 256 B**。
- 偏移必须等于 `sequence * 256`，且 `offset == 已收字节数`。
- 序号重复（丢 ACK）时 GD32 直接回 `ACK_OK` 不重复写 Flash。

### 3.4 包应答（`0x12`）

```
[0..1]   sequence (LE)
[2]      ACK 状态码
```

### 3.5 校验结果（`0x14`）

```
[0]      VERIFY 状态码
[1]      target_slot
[2..3]   新固件主版本 (LE)
[4..5]   新固件次版本 (LE)
```

### 3.6 检查更新应答（`0x22`，ESP32 → App）

```
[0]      available (0/1)
[1..2]   最新主版本 (LE)
[3..4]   最新次版本 (LE)
```
App 的 `CommTask` 收到后更新界面 `Latest: vX.Y (available)`。

## 4. `.fw` 升级包格式

```
┌──────────────────────┬───────────────────────────────┐
│ 40 B FirmwareHeader  │  App 原始 .bin（从向量表开始） │
└──────────────────────┴───────────────────────────────┘
```

- 头**只在传输中存在**，GD32 把 `payload` 数据写到槽起始地址 `0x08005000` / `0x08080000`，
  所以槽的第 0 字节仍是 MSP、第 4 字节仍是 Reset_Handler。
- `fwpack.py` 打包时会校验向量表：
  MSP 在 `0x20000000..0x20018000` 且 8 字节对齐；
  Reset_Handler 带 Thumb 位（`bit0=1`）且落在镜像范围内。
- `manifest.json`（供 ESP32）：
  ```json
  {
    "format": 1,
    "version": "1.1",
    "build_time": 1731945600,
    "packages": { "A": "app_a_v1.1.fw", "B": "app_b_v1.1.fw" }
  }
  ```

## 5. Boot 侧状态机

```
上电 → 初始化(时钟/串口/LCD/蜂鸣器/Flash) → 加载元数据
  ├─ request_upgrade=1  →  [WAIT_HEADER] 等待升级
  └─ 否则 → 选择槽 → 校验(CRC+向量表) → 跳转 App
                         ├─ 无有效槽 → [WAIT_HEADER]
                         └─ 试运行次数超限 → 标记 INVALID + 回退 + 失败音

[WAIT_HEADER]
  ├─ CMD_HANDSHAKE / CMD_VERSION_QUERY → 回应 0x02
  ├─ CMD_FIRMWARE_HEADER → 校验长度/容量 → 标记目标槽 INVALID
  │      → 擦目标槽 → ACK(0xFFFF) → [RECEIVE_DATA]
  └─ 60 s 无数据 → 清升级标志 + 复位回跳旧 App

[RECEIVE_DATA]
  ├─ CMD_FIRMWARE_DATA → 校验序号/长度 → 写 Flash → ACK
  └─ CMD_FIRMWARE_END → 整包 CRC32 + 向量表校验
         ├─ 失败 → 目标槽 INVALID + 失败音 + VERIFY_RESULT(非0) → [ERROR]
         └─ 成功 → 写 PENDING + pending_slot + 成功音
                    + VERIFY_RESULT(0) → 复位 → 试运行

试运行: trial_count++；App 的 Confirm 任务延时 5 s 后写 CONFIRMED
        └─ 若 3 次都没确认 → 标记 INVALID → 回退旧槽
```

## 6. 元数据（双页冗余，60 字节）

两个独立 2 KiB 页：`0x08004000`（A）、`0x08004800`（B）。
每次写**序号更大的一份**，另一份始终保持上一版本的完整有效副本。

```c
typedef struct {                 // 紧凑布局，共 60 B，无 padding
    uint32_t magic;              // 0x4D455441 ("META")
    uint16_t format_version;     // 1
    uint16_t length;             // 60
    uint32_t sequence;           // 递增，用于选出最新副本
    uint8_t  active_slot;        // 0=A, 1=B
    uint8_t  pending_slot;       // 0/1/0xFF(NONE)
    uint8_t  request_upgrade;    // App 写 1 触发进 Boot
    uint8_t  trial_count;        // 试运行次数，上限 3
    struct {
        uint32_t size;
        uint32_t crc32;
        uint16_t version_major;
        uint16_t version_minor;
        uint32_t build_time;
        uint8_t  state;          // EMPTY/CONFIRMED/PENDING/INVALID
        uint8_t  reserved[3];
    } slots[2];                  // 各 20 B
    uint32_t crc32;              // CRC32(前 56 字节)
} boot_metadata_t;
```

- `boot_metadata_save()`：选另一页 → `sequence+1` → 重算 CRC → 擦页 → 写 → **回读校验**。
- `boot_metadata_load()`：两页各校验 magic/版本/长度/CRC/字段合法性，取 `sequence` 更新的一份；
  都无效时用默认值（`active=A`，其余空）。

| 槽状态 | 值 | 含义 |
|--------|----|------|
| `EMPTY` | 0 | 从未写入 |
| `CONFIRMED` | 1 | 已确认可正常启动 |
| `PENDING` | 2 | 新写入，等待试运行确认 |
| `INVALID` | 3 | 校验失败 / 试运行失败 / 写入中 |

## 7. 可靠性约定

1. **先标记 `INVALID` 再擦写**：任何时刻断电，旧槽都完好且元数据不会指向半截镜像。
2. **只有整包 CRC + 向量表都通过**才写 `PENDING`/`pending_slot`，之后才复位。
3. **每包 ACK + 序号**：丢包/错 CRC 由上位机重发；重复序号 GD32 幂等回 ACK。
4. **不覆盖有效启动标志**：校验失败只把目标槽标 `INVALID`，`active_slot` 不动。
5. **试运行 + 回退**：`BOOT_MAX_TRIALS=3`，App 5 s 自检确认，未确认则回退。
6. **超时回跳**：Boot 处于 `WAIT_HEADER` 60 s 无数据，清标志并复位回旧 App。
7. **ESP32 断网不影响本地启动**：网关掉线只打印日志，GD32 完全自主决策。

## 8. 已知边界

- CRC 只防**传输误码**，不防恶意固件；量产需加签名/防回滚/安全启动。
- 无加密、无压缩；115200 波特率下 500 KiB 传输约 40 s 以上。
- 单包 256 B、payload 上限 300 B；提高波特率时建议同步评估串口环形缓冲（1024 B）。
- A/B 固件链接地址不同，**不能混刷**；上位机必须按握手返回的 `target_slot` 选包。
