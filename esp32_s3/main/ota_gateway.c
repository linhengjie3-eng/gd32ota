/*
 * ESP32-S3 Wi-Fi -> UART OTA 网关
 *
 * 职责边界: ESP32-S3 不碰 GD32 的 Flash, 只负责
 *   1) 连 Wi-Fi 拉取 manifest.json (版本号 + A/B 两个 .fw 文件名)
 *   2) 定期查询 GD32 版本/状态, 应答 App 的"检查更新"
 *   3) GD32 进入 Boot 升级模式后, 流式读取 .fw 包并按协议分包转发,
 *      每包等 ACK, 失败重传; 最后收校验结果并上报
 *
 * 采用"流式"而非"整包缓存": .fw 包头部已含固件总长和 CRC32,
 * 所以无需把最多 512KiB 的镜像放进 ESP32-S3 的 RAM/PSRAM。
 * 断网或网关掉电时 GD32 Boot 会在 60s 超时后自行回跳旧 App。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "sdkconfig.h"

#include "ota_protocol.h"
#include "ota_gateway.h"

#define TAG                     "ota_gw"
#define FW_HEADER_SIZE          40u        /* <IHHIII20s */
#define FW_MAGIC                0x4641574Cu
#define MANIFEST_BUFFER_SIZE    2048
#define UART_RX_BUFFER          2048
#define UART_RX_CHUNK           128
#define FRAME_QUEUE_LENGTH      16
#define WIFI_CONNECTED_BIT      BIT0
#define FW_IMAGE_MAX_SIZE       0x80000u   /* 512 KiB, Slot B 容量 */

typedef struct {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint32_t size;
    uint32_t crc32;
    uint32_t build_time;
} firmware_header_t;

static QueueHandle_t frame_queue;
static EventGroupHandle_t wifi_events;
static volatile bool wifi_connected;
static volatile bool gd32_online;
static volatile bool update_requested;
static volatile bool ota_in_progress;

static uint16_t running_major;
static uint16_t running_minor;
static uint16_t latest_major;
static uint16_t latest_minor;
static volatile bool manifest_ready;
static char package_a[128];
static char package_b[128];
static char manifest_version[32];

/* 只有 manifest 版本高于设备当前运行版本时才推送 */
static bool version_is_newer(uint16_t new_major, uint16_t new_minor,
                             uint16_t old_major, uint16_t old_minor)
{
    if (new_major != old_major) return new_major > old_major;
    return new_minor > old_minor;
}

/* 是否接受把 new 版本下发给设备。默认只允许升级；打开
   CONFIG_OTA_ALLOW_DOWNGRADE 后，任意不同版本（含降级）都可推送，
   用于反复演示 A/B 升降级（GD32 Boot 需同时以 --allow-downgrade 编译）。 */
static bool version_acceptable(uint16_t new_major, uint16_t new_minor,
                               uint16_t old_major, uint16_t old_minor)
{
#if CONFIG_OTA_ALLOW_DOWNGRADE
    return (new_major != old_major) || (new_minor != old_minor);
#else
    return version_is_newer(new_major, new_minor, old_major, old_minor);
#endif
}

/* ------------------------------------------------------------------ 小工具 */
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void parse_fw_header(const uint8_t *raw, firmware_header_t *out)
{
    out->magic      = read_le32(&raw[0]);
    out->major      = read_le16(&raw[4]);
    out->minor      = read_le16(&raw[6]);
    out->size       = read_le32(&raw[8]);
    out->crc32      = read_le32(&raw[12]);
    out->build_time = read_le32(&raw[16]);
}

static bool parse_version(const char *text, uint16_t *major, uint16_t *minor)
{
    unsigned a = 0u, b = 0u;
    if (text == NULL || sscanf(text, "%u.%u", &a, &b) != 2 ||
        a > 65535u || b > 65535u) {
        return false;
    }
    *major = (uint16_t)a;
    *minor = (uint16_t)b;
    return true;
}

/* -------------------------------------------------------------------- UART */
static void uart_init(void)
{
    uart_config_t config = {
        .baud_rate  = CONFIG_OTA_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(CONFIG_OTA_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(CONFIG_OTA_UART_PORT,
                                 CONFIG_OTA_UART_TX_GPIO,
                                 CONFIG_OTA_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_OTA_UART_PORT,
                                        UART_RX_BUFFER, UART_RX_BUFFER,
                                        0, NULL, 0));
    ESP_LOGI(TAG, "UART%d TX=%d RX=%d @ %d",
             CONFIG_OTA_UART_PORT, CONFIG_OTA_UART_TX_GPIO,
             CONFIG_OTA_UART_RX_GPIO, CONFIG_OTA_UART_BAUD);
}

static void send_frame(uint8_t command, const uint8_t *payload, uint16_t length)
{
    uint8_t buffer[PROTO_FRAME_MAX_SIZE];
    size_t size = ota_pack_frame(command, payload, length,
                                 buffer, sizeof(buffer));
    if (size > 0u) {
        uart_write_bytes(CONFIG_OTA_UART_PORT, (const char *)buffer, size);
    }
}

static void on_frame_received(const ota_frame_t *frame, void *context)
{
    (void)context;
    if (xQueueSend(frame_queue, frame, 0u) != pdTRUE) {
        ESP_LOGW(TAG, "frame queue full, cmd 0x%02X dropped", frame->command);
    }
}

static void uart_rx_task(void *argument)
{
    uint8_t buffer[UART_RX_CHUNK];
    ota_parser_t parser;
    (void)argument;

    ota_parser_init(&parser);
    for (;;) {
        int count = uart_read_bytes(CONFIG_OTA_UART_PORT, buffer,
                                    sizeof(buffer), pdMS_TO_TICKS(20));
        if (count > 0) {
            ota_parser_feed(&parser, buffer, (size_t)count,
                            on_frame_received, NULL);
        }
    }
}

/* ------------------------------------------------------- 帧等待与更新应答 */
static void reply_update_check(void);

static bool wait_frame(uint8_t command, ota_frame_t *out, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        TickType_t remaining = deadline - xTaskGetTickCount();
        ota_frame_t frame;
        if (xQueueReceive(frame_queue, &frame, remaining) != pdTRUE) {
            return false;
        }
        if (frame.command == command) {
            *out = frame;
            return true;
        }
        if (frame.command == CMD_UPDATE_CHECK) {
            reply_update_check();
        }
        /* 其余帧(例如迟到的 ACK)直接丢弃 */
    }
    return false;
}

static void reply_update_check(void)
{
    uint8_t payload[5];
    bool available = manifest_ready &&
                     version_acceptable(latest_major, latest_minor,
                                        running_major, running_minor);
    payload[0] = available ? 1u : 0u;
    payload[1] = (uint8_t)latest_major;
    payload[2] = (uint8_t)(latest_major >> 8);
    payload[3] = (uint8_t)latest_minor;
    payload[4] = (uint8_t)(latest_minor >> 8);
    send_frame(CMD_UPDATE_CHECK_RESP, payload, sizeof(payload));
    ESP_LOGI(TAG, "update check: running %u.%u, latest %u.%u, available=%d",
             running_major, running_minor, latest_major, latest_minor,
             available);
}

/* ------------------------------------------------------------------- Wi-Fi */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; GD32 keeps booting locally");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static void wifi_init(void)
{
    wifi_config_t config = {0};
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err;

    wifi_events = xEventGroupCreate();
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s",
             CONFIG_OTA_WIFI_SSID);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s",
             CONFIG_OTA_WIFI_PASSWORD);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* --------------------------------------------------------------- manifest */
static bool fetch_manifest(void)
{
    esp_http_client_config_t config = {
        .url = CONFIG_OTA_MANIFEST_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_OTA_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char buffer[MANIFEST_BUFFER_SIZE + 1];
    int total = 0;
    int status = 0;
    cJSON *root = NULL;
    const cJSON *version = NULL;
    const cJSON *packages = NULL;
    const cJSON *pkg_a = NULL;
    const cJSON *pkg_b = NULL;
    bool ok = false;

    if (client == NULL) return false;
    if (esp_http_client_open(client, 0) != ESP_OK) goto cleanup;
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "manifest HTTP status %d", status);
        goto cleanup;
    }
    while (total < MANIFEST_BUFFER_SIZE) {
        int n = esp_http_client_read(client, buffer + total,
                                     MANIFEST_BUFFER_SIZE - total);
        if (n <= 0) break;
        total += n;
    }
    buffer[total] = '\0';
    root = cJSON_Parse(buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "manifest is not valid JSON");
        goto cleanup;
    }
    version  = cJSON_GetObjectItem(root, "version");
    packages = cJSON_GetObjectItem(root, "packages");
    if (packages != NULL) {
        pkg_a = cJSON_GetObjectItem(packages, "A");
        pkg_b = cJSON_GetObjectItem(packages, "B");
    }
    if (!cJSON_IsString(version) || !cJSON_IsString(pkg_a) ||
        !cJSON_IsString(pkg_b)) {
        ESP_LOGE(TAG, "manifest missing version or packages.A/B");
        goto cleanup;
    }
    if (!parse_version(version->valuestring, &latest_major, &latest_minor)) {
        ESP_LOGE(TAG, "manifest version is invalid");
        goto cleanup;
    }
    snprintf(manifest_version, sizeof(manifest_version), "%s",
             version->valuestring);
    snprintf(package_a, sizeof(package_a), "%s", pkg_a->valuestring);
    snprintf(package_b, sizeof(package_b), "%s", pkg_b->valuestring);
    ESP_LOGI(TAG, "manifest v%u.%u  A=%s  B=%s", latest_major, latest_minor,
             package_a, package_b);
    manifest_ready = true;
    ok = true;

cleanup:
    if (root != NULL) cJSON_Delete(root);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* --------------------------------------------------------------- 结果上报 */
static void report_result(uint8_t verify_status, uint16_t major,
                          uint16_t minor, uint32_t size)
{
    ESP_LOGI(TAG, "GD32 verify result=%u version=%u.%u size=%lu",
             verify_status, major, minor, (unsigned long)size);
    if (strlen(CONFIG_OTA_REPORT_URL) == 0u) return;

    esp_http_client_config_t config = {
        .url = CONFIG_OTA_REPORT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_OTA_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char body[160];
    if (client == NULL) return;
    snprintf(body, sizeof(body),
             "{\"verify\":%u,\"version\":\"%u.%u\",\"size\":%lu}",
             verify_status, major, minor, (unsigned long)size);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "result report failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

/* ------------------------------------------------------------ HTTP 流读取 */
static bool http_read_exact(esp_http_client_handle_t client,
                            uint8_t *destination, size_t length)
{
    size_t done = 0u;
    while (done < length) {
        int n = esp_http_client_read(client, (char *)destination + done,
                                     (int)(length - done));
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

/* --------------------------------------------------------------- OTA 主体 */
static bool perform_ota(uint8_t target_slot)
{
    const char *filename = (target_slot == 0u) ? package_a : package_b;
    char url[320];
    uint8_t header_raw[FW_HEADER_SIZE];
    firmware_header_t header = {0};
    esp_http_client_config_t config;
    esp_http_client_handle_t client = NULL;
    ota_frame_t response;
    uint32_t offset = 0u;
    uint16_t sequence = 0u;
    uint8_t packet[2u + PROTO_DATA_CHUNK];
    uint8_t verify_status = 0xFFu;
    bool success = false;

    if (filename[0] == '\0') {
        ESP_LOGE(TAG, "no package configured for slot %c", 'A' + target_slot);
        return false;
    }
    if (snprintf(url, sizeof(url), "%s%s", CONFIG_OTA_FIRMWARE_BASE_URL,
                 filename) >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "firmware URL too long");
        return false;
    }

    memset(&config, 0, sizeof(config));
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = CONFIG_OTA_HTTP_TIMEOUT_MS;
    config.keep_alive_enable = true;
    client = esp_http_client_init(&config);
    if (client == NULL) return false;
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(TAG, "firmware HTTP status %d",
                 esp_http_client_get_status_code(client));
        goto done;
    }

    /* 1) .fw 包头部 40 字节已包含总长/CRC32/版本号 */
    if (!http_read_exact(client, header_raw, sizeof(header_raw))) {
        ESP_LOGE(TAG, "cannot read .fw header");
        goto done;
    }
    parse_fw_header(header_raw, &header);
    if ((header.magic != FW_MAGIC) || (header.size < 8u) ||
        (header.size > FW_IMAGE_MAX_SIZE)) {
        ESP_LOGE(TAG, "bad .fw header: magic=0x%08lX size=%lu",
                 (unsigned long)header.magic, (unsigned long)header.size);
        goto done;
    }
    ESP_LOGI(TAG, "pushing %s: slot %c, v%u.%u, %lu bytes",
             filename, 'A' + target_slot, header.major, header.minor,
             (unsigned long)header.size);

    /* 端到端版本校验：manifest 只是索引，真正写进 Flash 的是 .fw 头声明的版本。
       两者不一致时（例如 manifest 改成 1.3 但服务器仍放 v1.0 包）必须拒绝，
       否则 GD32 会把低版本当成新版本刷进去，设备侧也拦不住"包与清单不符"。 */
    if (manifest_ready &&
        (header.major != latest_major || header.minor != latest_minor)) {
        ESP_LOGE(TAG, "package v%u.%u disagrees with manifest v%u.%u, abort",
                 header.major, header.minor, latest_major, latest_minor);
        goto cleanup;
    }
    if (!version_acceptable(header.major, header.minor,
                            running_major, running_minor)) {
        ESP_LOGE(TAG, "package v%u.%u not acceptable vs running v%u.%u, abort",
                 header.major, header.minor, running_major, running_minor);
        goto cleanup;
    }

    /* 2) 固件头帧, GD32 收到后才擦除目标槽 */
    send_frame(CMD_FIRMWARE_HEADER, header_raw, FW_HEADER_SIZE);
    if (!wait_frame(CMD_PACKET_ACK, &response,
                    (uint32_t)CONFIG_OTA_ACK_TIMEOUT_MS * 3u) ||
        response.length < 3u ||
        read_le16(response.payload) != PROTO_HEADER_ACK_SEQ ||
        response.payload[2] != ACK_OK) {
        ESP_LOGE(TAG, "firmware header rejected");
        goto done;
    }

    /* 3) 边下载边分包转发, 每包等 ACK, 失败重传 */
    while (offset < header.size) {
        uint32_t want = header.size - offset;
        bool acked = false;
        unsigned attempt;

        if (want > PROTO_DATA_CHUNK) want = PROTO_DATA_CHUNK;
        if (!http_read_exact(client, &packet[2], want)) {
            ESP_LOGE(TAG, "short read at offset %lu", (unsigned long)offset);
            goto done;
        }
        packet[0] = (uint8_t)sequence;
        packet[1] = (uint8_t)(sequence >> 8);

        for (attempt = 0u;
             attempt < (unsigned)CONFIG_OTA_MAX_RETRIES && !acked;
             ++attempt) {
            send_frame(CMD_FIRMWARE_DATA, packet, (uint16_t)(2u + want));
            if (wait_frame(CMD_PACKET_ACK, &response,
                           CONFIG_OTA_ACK_TIMEOUT_MS) &&
                response.length >= 3u &&
                read_le16(response.payload) == sequence &&
                response.payload[2] == ACK_OK) {
                acked = true;
            }
        }
        if (!acked) {
            ESP_LOGE(TAG, "packet %u NAK/timeout at offset %lu",
                     sequence, (unsigned long)offset);
            goto done;
        }
        offset += want;
        sequence++;
        if ((sequence % 16u) == 0u || offset == header.size) {
            ESP_LOGI(TAG, "progress %lu%%",
                     (unsigned long)(offset * 100u / header.size));
        }
    }

    /* 4) 结束帧 + 校验结果 */
    send_frame(CMD_FIRMWARE_END, NULL, 0);
    if (!wait_frame(CMD_VERIFY_RESULT, &response, 15000u) ||
        response.length < 1u) {
        ESP_LOGE(TAG, "no verify result from GD32");
        goto done;
    }
    verify_status = response.payload[0];
    success = (verify_status == 0u);
    ESP_LOGI(TAG, "verify %s (status %u), GD32 will reset into trial slot",
             success ? "OK" : "FAILED", verify_status);

done:
    report_result(verify_status, header.major, header.minor, header.size);
cleanup:
    /* 版本不符是网关侧主动中止，不伪装成 GD32 的 VERIFY_RESULT 上报 */
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    return success;
}

/* --------------------------------------------------------------- 主循环 */
static void gateway_task(void *argument)
{
    TickType_t last_manifest = 0u;
    ota_frame_t frame;
    (void)argument;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (wifi_connected &&
            (last_manifest == 0u ||
             (now - last_manifest) > pdMS_TO_TICKS(60000))) {
            if (fetch_manifest()) last_manifest = now;
        }

        send_frame(CMD_VERSION_QUERY, NULL, 0);
        if (wait_frame(CMD_HANDSHAKE_RESP, &frame, 1200u) &&
            frame.length >= 12u) {
            uint8_t state = frame.payload[2];
            uint8_t target = frame.payload[3];
            gd32_online = true;
            running_major = read_le16(&frame.payload[4]);
            running_minor = read_le16(&frame.payload[6]);
            ESP_LOGI(TAG, "GD32 state=%u slot=%c running v%u.%u",
                     state, (target < 2u) ? ('A' + target) : '?',
                     running_major, running_minor);

            if ((state == DEVICE_STATE_BOOT_WAIT) && !ota_in_progress &&
                (target <= 1u)) {
#if CONFIG_OTA_AUTO_PUSH
                update_requested = true;
#endif
                const bool newer = manifest_ready &&
                    version_acceptable(latest_major, latest_minor,
                                       running_major, running_minor);
                if (!newer) {
                    ESP_LOGI(TAG, "GD32 v%u.%u already up to date (server v%u.%u), nothing to do",
                             running_major, running_minor,
                             latest_major, latest_minor);
                    update_requested = false;
                } else if (update_requested) {
                    update_requested = false;
                    ota_in_progress = true;
                    ESP_LOGI(TAG, "update v%u.%u -> v%u.%u", running_major,
                             running_minor, latest_major, latest_minor);
                    perform_ota(target);
                    ota_in_progress = false;
                } else {
                    ESP_LOGW(TAG, "GD32 is in Boot mode; auto push disabled");
                }
            }
        } else {
            gd32_online = false;
            ESP_LOGW(TAG, "GD32 not responding (Boot in transfer, or offline)");
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_OTA_POLL_INTERVAL_MS));
    }
}

/* ------------------------------------------------------------------ 公共 API */
void ota_gateway_request_update(void)
{
    update_requested = true;
}

bool ota_gateway_wifi_connected(void)
{
    return wifi_connected;
}

bool ota_gateway_gd32_online(void)
{
    return gd32_online;
}

void app_main(void)
{
    ESP_LOGI(TAG, "GD32F303 A/B OTA gateway (ESP32-S3)");
    frame_queue = xQueueCreate(FRAME_QUEUE_LENGTH, sizeof(ota_frame_t));
    configASSERT(frame_queue != NULL);

    uart_init();
    wifi_init();
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);
    xTaskCreate(gateway_task, "gateway", 8192, NULL, 4, NULL);
}
