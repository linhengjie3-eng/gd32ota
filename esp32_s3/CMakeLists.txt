cmake_minimum_required(VERSION 3.16)

# ESP32-S3 Wi-Fi -> UART OTA 网关
# 构建: idf.py set-target esp32s3 && idf.py build flash monitor
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gd32_ota_gateway)
