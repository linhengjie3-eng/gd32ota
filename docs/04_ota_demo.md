# 04 · 演示流程与压测

> 前置：已按 [01](01_environment_setup.md) 装好环境、`python tools/build.py all` 成功、按 [03](03_pinout.md) 接好线。

## 0. 先分清两路串口（最容易接错）

| 串口 | GD32 引脚 | 用途 | 谁接 |
|------|-----------|------|------|
| **USART0** | PA9 / PA10 | **调试日志 printf** | DAPLink 自带串口（`COM4`），或 USB-TTL #1 |
| **USART1** | PA2 / PA3 | **OTA 协议帧** | ESP32-S3，或做 PC 串口升级时接 USB-TTL #2 |

- Boot 的升级协议跑在 **USART1**。`tools/serial_ota.py` 必须接 **PA2/PA3**，不是 PA9/PA10。
- **本机已用 DAPLink 自带串口（`COM4`）承担 USART0 日志**，所以做 PC 串口 OTA 只需**再买一个 USB-TTL**
  接 PA2/PA3 发升级包；若不买，直接跳到接 ESP32-S3 走 Wi-Fi OTA。
- 两路都要与 GD32 **共地**。

---

## 1. 出厂烧录与默认启动（验收项 1）

```bash
python tools/build.py flash-factory    # 烧 1 MiB 整片镜像到 0x08000000
```

打开 PA9/PA10 的串口（115200 8N1），复位，应看到：

```
GD32F303 Boot 1.0, A/B OTA
Jump slot A, v1.0, 123456 bytes

GD32F303 App 1.0, slot A
Application slot A confirmed
```

屏幕显示主页面：标题、当前版本号、联网状态（未接 ESP32 时显示 OFFLINE），
点 "Firmware update" 能进升级页，触摸有效，**不 HardFault**。

---

## 2. App 触发升级进入 Boot（验收项 2）

在 App 主页面点 `Firmware update` → 升级页 → `Enter upgrade mode`：

1. App 写 `request_upgrade=1` 到元数据；
2. 蜂鸣器短滴；
3. `NVIC_SystemReset()` 软复位回 Boot；
4. Boot 看到升级请求，**不跳转 App**，显示：
   ```
   GD32F303 A/B OTA
   BOOT v1.0
   WAITING FIRMWARE
   进度条 0%
   ```
5. 串口日志：`GD32F303 Boot 1.0, A/B OTA`（不再有 `Jump slot ...`）。

---

## 3. PC 串口 OTA（推荐先做，分层排障）

**不接 ESP32-S3**，把第二个 USB-TTL 接到 PA2/PA3：

```
USB-TTL#2 TX  -> PA3 (USART1_RX)
USB-TTL#2 RX  <- PA2 (USART1_TX)
GND           -> GND
```

### 3.1 先确认目标槽

Boot 握手时会告诉上位机它要写哪个槽。也可以直接推理：
`factory.bin` 里活动槽是 **A(CONFIRMED)**，所以第一次升级的目标是 **B**。

```bash
python tools/serial_ota.py --port COM6 --slot B \
    --package build/packages/app_b_v1.0.fw --yes
```

> COM6 换成你设备管理器的实际端口；`--yes` 是确认擦除的开关（不加则只做 dry run）。
> 工具会先握手核对 Boot 返回的 target slot，**不一致会直接报错退出**，防止 A/B 固件混刷。

### 3.2 正常输出

```
GD32 protocol=0x0100 state=1 target=B
  0%  16%  32% ... 100%
upgrade accepted: v1.0, 123456 bytes in 42.3s; GD32 will reset
```

随后 Boot 复位进入**试运行槽 B**，5 秒后 App 的 `Confirm` 任务写 `CONFIRMED`。
串口应看到 `Jump slot B, ...` 与 `Application slot B confirmed`，
蜂鸣器短双音，屏幕显示 `Slot B`。

### 3.3 A/B 交替（验收项 3、6）

| 次数 | 当前活动槽 | 目标槽 | 发布包 |
|------|------------|--------|--------|
| 1 | A | B | `app_b_v1.1.fw` |
| 2 | B | A | `app_a_v1.2.fw` |
| 3 | A | B | `app_b_v1.3.fw` |

每次用 `python tools/build.py --version x.y all` 重新出包，两个槽的 `.fw` 都会更新。

> **绝对不能**把 `app_a_*.fw` 发到目标 B（或反之）：两个固件的链接地址不同，
> 向量表里的绝对地址不同，混刷会导致跳转后立刻 HardFault。
> PC 工具与 ESP32 都会核对 slot，别绕过这个检查。

### 3.4 版本号演示（v1.0 → v1.1 → v1.2 → 回到 v1.0）

这套流程回答三个问题：**版本号怎么真的变**、**为什么能一直往上加**、
**怎么回到最初版本**。先跑只读检查：

```bash
python tools/build.py version-status
```

它会打印「出厂镜像 / App-A / App-B / manifest / 各 `.fw` 头」五项版本，
不一致会列出具体哪一项。以下是**离线只读**工具，不连串口、不烧录。

| 步 | 命令 | 预期屏幕 |
|----|------|----------|
| 1 · 回到最初版本 | `python tools/build.py --version 1.0 flash-factory` | `Application v1.0  Slot A` |
| 2 · 发布 v1.1 | `python tools/build.py --version 1.1 all` + 起服务器 + 界面点升级 | `Application v1.1  Slot B` |
| 3 · 发布 v1.2 | `python tools/build.py --version 1.2 all` + 重复点升级 | `Application v1.2  Slot A` |
| 4 · 想重来 | 回到第 1 步 | — |

**为什么版本号现在会真的变**：`--version X.Y` 同时写入三处 ——
`.fw` 头、factory 元数据、以及 App 源码里的 `APP_VERSION_MAJOR/MINOR` 宏
（决定屏幕上显示的字符串）。`tools/build.py` 的 `compile.flags` 指纹包含
版本号宏，改版本会**清空并全量重编**目标文件，不会再复用旧 `.o`。
`factory` / `packages` / `flash-factory` 还会核对二进制的编译版本与
`--version` 是否一致，不一致直接报错，杜绝"包标着 v1.2、刷进去屏幕还是 v1.1"。

**为什么 A/B 会交替**：Boot 通过握手把「非活动槽」告诉上位机，
新固件只写那个槽，旧槽全程不动。所以 A→B→A→B，屏幕上的槽号与版本号
会同时变化，这就是升级确实发生的证据。

**为什么能一直往上加**：版本号没有上限，`--version 1.3 all`、
`--version 2.0 all` 都可以。设备升到 v1.2 后，服务器仍是 v1.2 时网关打印
`already up to date, nothing to do`；把服务器换成 v1.3 再点升级才会升。

**怎么回到最初版本**：用第 1 步的 **SWD 出厂烧录**，不要用 OTA 降级。
`flash-factory` 会重写整片镜像与元数据，把 `active_slot` 复位到 A；
OTA 通道默认拒绝降级（防回滚），三处校验见 [05 文档](05_protocol.md)。

> ⚠️ 第 1 步是不可逆烧录，会覆盖板内原有程序，执行前请确认。

---

## 4. Wi-Fi OTA（验收项 3）

### 4.1 起本地固件服务器

```bash
python tools/firmware_server.py --root build/packages --port 8000
# Serving ... on http://0.0.0.0:8000
```

浏览器打开 `http://<PC局域网IP>:8000/manifest.json`，应看到：

```json
{
  "format": 1,
  "version": "1.0",
  "build_time": 1731945600,
  "packages": {
    "A": "app_a_v1.0.fw",
    "B": "app_b_v1.0.fw"
  }
}
```

### 4.2 配置并烧 ESP32-S3

```bash
cd esp32_s3
idf.py set-target esp32s3
idf.py menuconfig       # GD32 OTA Gateway: 填 SSID/密码/manifest URL/base URL/UART 引脚
idf.py build
idf.py -p COMxx flash monitor
```

- `OTA_MANIFEST_URL` = `http://<PC-IP>:8000/manifest.json`
- `OTA_FIRMWARE_BASE_URL` = `http://<PC-IP>:8000/`（**末尾要有 `/`**）
- `OTA_REPORT_URL` 可填 `http://<PC-IP>:8000/report`，服务器会打印上报结果
- 不能用 `127.0.0.1`（那是 ESP32 自己）

### 4.3 触发

1. App 主页面点 `Check update` → App 发 `CMD_UPDATE_CHECK`，
   ESP32 用 manifest 的版本号回 `CMD_UPDATE_CHECK_RESP`，界面显示 `Latest: vX.Y (available)`。
2. 点 `Enter upgrade mode` → 复位进 Boot。
3. ESP32 每 2 s 轮询一次 `CMD_VERSION_QUERY`，发现 `state=BOOT_WAIT` 且
   `CONFIG_OTA_AUTO_PUSH=y`，自动按目标槽拉取对应 `.fw` 并流式分包下发。
4. ESP32 日志：
   ```
   manifest v1.1  A=app_a_v1.1.fw  B=app_b_v1.1.fw
   GD32 state=1 slot=B running v1.0
   pushing app_b_v1.1.fw: slot B, v1.1, 124000 bytes
   progress 25% ... 100%
   verify OK (status 0), GD32 will reset into trial slot
   ```
5. GD32 侧与 PC 串口 OTA 完全一致：CRC 通过 → 试运行 → 确认。

> **断网不影响本地启动**：ESP32 掉线只打印 `Wi-Fi disconnected`；
> GD32 Boot 在 60 s 内没收到固件头，会自动清升级标志并复位回跳旧 App。

---

## 5. 校验失败测试（验收项 4）

用工具自带的故障注入开关（只把 `.fw` 头里的 `crc32` 字段改错，镜像本身不动，
这样包能通过 PC 侧检查，但 GD32 收完后整包 CRC 一定对不上）：

```bash
python tools/serial_ota.py --port COM6 --slot B \
    --package build/packages/app_b_v1.0.fw --yes --force-bad-crc
```

PC 工具最后会打印 `error: verification failed with status 1`，**这是预期结果**。

预期结果：

- Boot 显示 `VERIFY FAILED`，蜂鸣器**长低音 650 ms**；
- 目标槽被标 `INVALID`，`pending_slot` 清空；
- 旧槽（A）**完好**，复位后仍跳回旧 App；
- 可以重新进入升级模式再升一次（可重升）。

> 主机侧 `fwpack.py verify` 与 `python tools/build.py test` 里的篡改用例覆盖的是同一套 CRC 逻辑。

---

## 6. 断电恢复测试（验收项 9）

| 时机 | 操作 | 预期 |
|------|------|------|
| 传输到 30% | 拔 GD32 电源再上电 | 旧 App 正常启动；新槽是半截但标 `INVALID`，Boot 不会跳它 |
| 擦完槽、还没收完 | 断电 | 同上 |
| 写完但没收到 `CMD_FIRMWARE_END` | 断电 | 元数据仍是 `INVALID`，旧槽有效 |
| 写完且校验通过、复位瞬间 | 断电 | 元数据已写 `PENDING`，重启后进入试运行；若新 App 起不来，3 次后回退 |

每轮之后读串口日志确认 `Jump slot A/...`，并确认界面版本号没变。

---

## 7. 试运行失败 / 自动回退（A/B 的核心价值）

模拟"新 App 能跳转但跑不起来"：临时把 `app/src/main.c` 的 `Confirm` 任务改成
`vTaskDelay(portMAX_DELAY)`（永不确认），编译成新包升级。

预期：

1. 每次上电 Boot 看到 `pending_slot` 有效且 `trial_count < 3`，仍跳新槽；
2. App 卡住 / 看门狗或手动复位，`trial_count` 递增；
3. 第 3 次后 Boot 把新槽标 `INVALID`、清 `pending_slot`，打印
   `Trial limit reached, rollback to slot A`，鸣失败音；
4. 之后稳定跳回旧槽。

恢复后再升级一个"会正常 Confirm"的包，应重新成功。

---

## 8. 压测清单（验收项 9）

- [ ] 连续 A→B→A→B 升级 10 次，每次界面版本号与槽号正确
- [ ] 每次故意在第 N 包断开串口线，重连后重升成功
- [ ] 升级过程中随机断电 5 次，均不影响旧 App
- [ ] 故意发错误 CRC 的包 3 次，均保留旧固件且可重升
- [ ] 拔掉 ESP32-S3 / 关 Wi-Fi，Boot 60 s 超时后自动回跳旧 App
- [ ] 全程串口日志无 `HardFault` / 跑飞；跳转后触摸、串口中断正常

---

## 9. 验收标准对照（原始需求）

| 验收项 | 对应场景 |
|--------|----------|
| 1 上电默认跳现有 APP，界面正常，不 HardFault | §1 |
| 2 APP 触发升级后重启进 Boot，显示 Logo/版本号/进度 | §2 |
| 3 串口/ESP32 收新固件，校验通过写入并运行新版本 | §3 §4 |
| 4 校验失败蜂鸣器报错、保留旧固件、可重升 | §5 |
| 5 跳转后中断正常（VTOR 正确），日志无跑飞 | §1 §8（跳转前已恢复 `__enable_irq()`） |
| 6 两工程地址不重叠，Flash 不超容量 | `python tools/build.py test` + [02 文档](02_build_and_flash.md) §3 |
