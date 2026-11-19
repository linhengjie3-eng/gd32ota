#ifndef OTA_GATEWAY_H
#define OTA_GATEWAY_H

#include <stdbool.h>
#include <stdint.h>

/* 供外部(按键/HTTP/串口命令)请求一次升级; Boot 模式就绪后自动推送 */
void ota_gateway_request_update(void);
bool ota_gateway_wifi_connected(void);
bool ota_gateway_gd32_online(void);

#endif
