# ESP32-S3 Wi-Fi OTA 网关

ESP-IDF 工程，负责连 Wi-Fi → 读 `manifest.json` → 流式下载 `.fw` → 按协议分包从 UART 转发给 GD32，
等 ACK、失败重发，最后收校验结果并上报。**不写 GD32 的 Flash**。

## 1. 环境

- **ESP-IDF v5.0+**（用了 `uart_config_t.source_clk`、`esp_event_handler_instance_register` 等 5.x API）
- ESP32-S3 开发板（默认 UART1：TX=GPIO17、RX=GPIO18）
- 2.4 GHz Wi-Fi（ESP32-S3 不支持 5 GHz）

用 ESP-IDF Windows Installer 装好后，打开 **"ESP-IDF CMD"** 终端（会自动设置 `IDF_PATH` / PATH）。

## 2. 编译与烧录

```bash
cd esp32_s3
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMxx flash monitor
```

`menuconfig` 里的 **`GD32 OTA Gateway`** 菜单：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `OTA_WIFI_SSID` | `YOUR_WIFI_SSID` | 改成你的 2.4G Wi-Fi |
| `OTA_WIFI_PASSWORD` | `YOUR_WIFI_PASSWORD` | |
| `OTA_MANIFEST_URL` | `http://192.168.1.10:8000/manifest.json` | 改成 PC 局域网 IP |
| `OTA_FIRMWARE_BASE_URL` | `http://192.168.1.10:8000/` | **末尾必须有 `/`** |
| `OTA_REPORT_URL` | 空 | 填 `http://<PC-IP>:8000/report` 可收到结果 POST |
| `OTA_UART_TX_GPIO` | 17 | 接 GD32 `PA3` (USART1_RX) |
| `OTA_UART_RX_GPIO` | 18 | 接 GD32 `PA2` (USART1_TX) |
| `OTA_UART_BAUD` | 115200 | 必须与 GD32 一致 |
| `OTA_UART_PORT` | 1 | ESP32-S3 UART 编号 |
| `OTA_POLL_INTERVAL_MS` | 2000 | 版本查询周期 |
| `OTA_HTTP_TIMEOUT_MS` | 30000 | HTTP 超时 |
| `OTA_ACK_TIMEOUT_MS` | 3000 | 等 GD32 ACK 超时 |
| `OTA_MAX_RETRIES` | 6 | 单包最大重发次数 |
| `OTA_AUTO_PUSH` | y | 检测到 Boot 等待模式时自动推送 |

> 不要用 `127.0.0.1`：那指向 ESP32 自己。用 PC 在局域网里的真实 IP。

## 3. 接线

| ESP32-S3 | GD32F303VGT6 | 说明 |
|----------|--------------|------|
| GPIO17 (TX) | `PA3` (USART1_RX) | 交叉 |
| GPIO18 (RX) | `PA2` (USART1_TX) | 交叉 |
| GND | GND | **必须共地** |
| 3.3V | 独立供电 | 不要从 GD32 板取大电流 |

两侧都是 3.3 V 电平，无需转换。GD32 侧的引脚定义在 `config/board_config.h`。

## 4. 服务器与包格式

PC 上起服务：

```bash
python tools/firmware_server.py --root build/packages --port 8000
```

`build/packages/manifest.json` 由 `tools/fwpack.py` 生成：

```json
{
  "format": 1,
  "version": "1.1",
  "build_time": 1731945600,
  "packages": { "A": "app_a_v1.1.fw", "B": "app_b_v1.1.fw" }
}
```

网关用 `CONFIG_OTA_FIRMWARE_BASE_URL + packages[target_slot]` 拼出完整 URL。
`target_slot` 来自 GD32 握手应答的 `payload[3]` —— **A/B 固件不能混刷**。

`.fw` = 40 字节头（含 `firmware_size` 与 `crc32`）+ 原始 App 镜像。
协议细节见 [docs/05_protocol.md](../docs/05_protocol.md)。

## 5. 工作流程

```
app_main
  ├─ nvs / netif / event loop / Wi-Fi STA 连接
  ├─ UART1 初始化
  ├─ uart_rx_task  : 持续读 UART → 帧解析 → 投递到队列
  └─ gateway_task  :
       1. 每 60 s 拉一次 manifest（更新 latest_major/minor 与 A/B 文件名）
       2. 每 2 s 发 CMD_VERSION_QUERY，收 CMD_HANDSHAKE_RESP
          - state=APP     → 记录当前版本
          - 收到 CMD_UPDATE_CHECK → 回 CMD_UPDATE_CHECK_RESP(最新版本号)
          - state=BOOT_WAIT → 自动推送（或等 ota_gateway_request_update()）
       3. perform_ota():
          HTTP GET .fw → 读 40 B 头 → CMD_FIRMWARE_HEADER → ACK
          → 边读边按 256 B 分包 CMD_FIRMWARE_DATA → 每包等 ACK/重发
          → CMD_FIRMWARE_END → CMD_VERIFY_RESULT → 上报
```

### 为什么用"流式"而不是整包缓存

`.fw` 包最多 512 KiB，ESP32-S3 无 PSRAM 时可用 heap 装不下。
头里已经带了总长和 CRC32，所以网关只需缓存**当前 256 B 包**即可边下边发，
重发时直接用缓存，不需要回退 HTTP 流。内存占用极小，也不依赖分区表。

## 6. 断网与异常

| 情况 | 行为 |
|------|------|
| Wi-Fi 断开 | 打印 `Wi-Fi disconnected; GD32 keeps booting locally`，自动重连；**不影响 GD32 本地启动** |
| manifest 拉取失败 | 保留上一次版本号，继续轮询，不触发升级 |
| 服务器中断 | `http_read_exact` 失败 → 中止本轮升级；GD32 保留旧固件，可重升 |
| 某包 ACK 超时/NAK | 最多重发 `OTA_MAX_RETRIES` 次 |
| GD32 返回校验失败 | 打印 `verify FAILED`，POST 上报（若配置），不改动 GD32 元数据 |
| GD32 在 Boot 但没包头 | Boot 60 s 超时自动回跳旧 App |

## 7. 对外 API（供按键/HTTP 触发）

```c
#include "ota_gateway.h"

ota_gateway_request_update();        // 请求一次升级（Boot 就绪后推送）
bool ota_gateway_wifi_connected(void);
bool ota_gateway_gd32_online(void);
```

关闭 `CONFIG_OTA_AUTO_PUSH` 后，可把 `ota_gateway_request_update()` 挂到按键、
本地 HTTP server 或 MQTT 回调上，实现"人工确认才升级"。

## 8. 故障排查

| 现象 | 检查 |
|------|------|
| `idf.py build` 报 `uart_config_t` 无 `source_clk` | 用了 IDF 4.x，请升级到 5.0+ |
| 一直 `Wi-Fi disconnected` | SSID/密码、是否 2.4 GHz、是否开了 MAC 过滤 |
| `manifest HTTP status 404` | URL 写错、服务器没起、`--root` 指错目录 |
| `manifest missing version or packages.A/B` | 用 `fwpack.py pack --manifest` 生成，别手写错字段 |
| `no package configured for slot X` | manifest 里缺 A 或 B；两个槽的包都要打包 |
| `pushing ...` 后卡住 | GD32 是否在 Boot 等待模式；UART TX/RX 是否接反；共地；波特率 |
| `packet N NAK/timeout` | 串口线过长/干扰、降波特率、增大 `OTA_ACK_TIMEOUT_MS` |
| GD32 一直不进 Boot | 确认 App 点了 "Enter upgrade mode"、`BOOT0=0`、元数据写成功 |
| 版本号显示不对 | manifest 的 `version` 格式必须是 `MAJOR.MINOR`（如 `1.1`） |

## 9. 与 PC 工具的关系

`tools/serial_ota.py` 与本项目使用**完全相同的协议**，可以直接把 ESP32-S3 换成 USB-TTL
接到 GD32 的 PA2/PA3，先不碰 Wi-Fi 就把 GD32 侧的擦写/CRC/回退逻辑验证完。
`python tools/build.py test` 会断言三方常量一致。
