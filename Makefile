# =============================================================================
#  GD32F303VGT6  A/B 双分区 IAP/OTA  ——  顶层构建脚本
#
#  工具链: Arm GNU Toolchain (arm-none-eabi-gcc) + GNU Make + Python 3
#  常用命令:
#      make help                 查看全部目标
#      make check-toolchain      检查工具链是否装好
#      make all                  Boot / App-A / App-B / factory.bin / .fw 包
#      make VERSION=1.1 all     以版本号 1.1 构建升级包
#      make test                 主机侧协议/CRC/分区/打包自测 (无需硬件)
#      make flash-factory        用 OpenOCD 烧录出厂整片镜像
#
#  目录约定: 必须在项目根目录执行 make (即本 Makefile 所在目录)。
# =============================================================================

SHELL := /bin/sh

PREFIX   ?= arm-none-eabi-
GCC_PATH ?=
TC       := $(if $(GCC_PATH),$(GCC_PATH)/,)
CC       := $(TC)$(PREFIX)gcc
CP       := $(TC)$(PREFIX)objcopy
SZ       := $(TC)$(PREFIX)size
PY       ?= python

BUILD    ?= build
VERSION  ?= 1.0
V_MAJOR  := $(word 1,$(subst ., ,$(VERSION)))
V_MINOR  := $(word 2,$(subst ., ,$(VERSION)))

# ------------------------------------------------------------------ 依赖路径
LIBROOT := third_party/gd32f30x/Firmware
CMSIS   := $(LIBROOT)/CMSIS
GDINC   := $(CMSIS)/GD/GD32F30x/Include
GDSRC   := $(CMSIS)/GD/GD32F30x/Source
PERIPH  := $(LIBROOT)/GD32F30x_standard_peripheral
RTOS    := third_party/FreeRTOS-Kernel
LVGLDIR := third_party/lvgl
STARTUP := $(GDSRC)/GCC/startup_gd32f30x_xd.S

# -------------------------------------------------------------------- 源文件
GD32_C   := $(sort $(wildcard $(PERIPH)/Source/*.c)) \
            $(GDSRC)/system_gd32f30x.c

COMMON_C := \
    common/bsp/bsp_board.c \
    common/bsp/bsp_uart.c \
    common/bsp/bsp_spi.c \
    common/bsp/bsp_lcd.c \
    common/bsp/bsp_flash.c \
    common/bsp/syscalls.c \
    common/protocol/crc.c \
    common/protocol/protocol.c \
    common/boot_shared/boot_metadata.c \
    common/boot_shared/boot_policy.c \
    common/boot_shared/boot_jump.c

BOOT_C   := boot/src/main.c boot/src/systick.c $(COMMON_C) $(GD32_C)
APP_C    := $(sort $(wildcard app/src/*.c)) \
            $(COMMON_C) common/bsp/bsp_touch.c $(GD32_C)

RTOS_C   := $(RTOS)/tasks.c $(RTOS)/queue.c $(RTOS)/list.c \
            $(RTOS)/portable/GCC/ARM_CM4F/port.c \
            $(RTOS)/portable/MemMang/heap_4.c

LVGL_C   := $(sort \
            $(wildcard $(LVGLDIR)/src/*.c) \
            $(wildcard $(LVGLDIR)/src/*/*.c) \
            $(wildcard $(LVGLDIR)/src/*/*/*.c) \
            $(wildcard $(LVGLDIR)/src/*/*/*/*.c))

# --------------------------------------------------------------- 编译/链接参数
MCU     := -mcpu=cortex-m4 -mthumb
DEFS    := -DUSE_STDPERIPH_DRIVER -DGD32F30X_XD -DHXTAL_VALUE=8000000
OPT     := -Os -g -gdwarf-2
WARN    := -Wall -Wextra -Wno-unused-parameter

INCLUDES := \
    -Iconfig \
    -Icommon/bsp -Icommon/protocol -Icommon/boot_shared \
    -Iapp/src -Iboot/src \
    -I$(CMSIS) -I$(GDINC) -I$(PERIPH)/Include \
    -I$(RTOS)/include -I$(RTOS)/portable/GCC/ARM_CM4F \
    -I$(LVGLDIR)

CFLAGS   := $(MCU) $(DEFS) $(INCLUDES) $(OPT) $(WARN) \
            -fdata-sections -ffunction-sections -std=gnu99 -MMD -MP
ASFLAGS  := $(MCU) $(DEFS) $(INCLUDES) $(OPT) -Wall \
            -x assembler-with-cpp -fdata-sections -ffunction-sections -MMD -MP

LDFLAGS  := $(MCU) -specs=nano.specs -specs=nosys.specs \
            -Wl,--gc-sections -Wl,--print-memory-usage \
            -Wl,--no-warn-rwx-segments \
            -lc -lm -lgcc -lnosys

BOOT_DEFS := -DBOOT_BUILD=1
APPA_DEFS := -DAPP_VERSION_MAJOR=$(V_MAJOR) -DAPP_VERSION_MINOR=$(V_MINOR)
APPB_DEFS := -DAPP_SLOT_B \
             -DAPP_VERSION_MAJOR=$(V_MAJOR) -DAPP_VERSION_MINOR=$(V_MINOR)

# ------------------------------------------------------------------- 对象列表
BOOT_OBJ := $(addprefix $(BUILD)/boot/obj/,$(BOOT_C:.c=.o)) \
            $(BUILD)/boot/obj/startup.o
APPA_OBJ := $(addprefix $(BUILD)/app_a/obj/,$(APP_C:.c=.o)) \
            $(addprefix $(BUILD)/app_a/obj/,$(RTOS_C:.c=.o)) \
            $(addprefix $(BUILD)/app_a/obj/,$(LVGL_C:.c=.o)) \
            $(BUILD)/app_a/obj/startup.o
APPB_OBJ := $(addprefix $(BUILD)/app_b/obj/,$(APP_C:.c=.o)) \
            $(addprefix $(BUILD)/app_b/obj/,$(RTOS_C:.c=.o)) \
            $(addprefix $(BUILD)/app_b/obj/,$(LVGL_C:.c=.o)) \
            $(BUILD)/app_b/obj/startup.o

.PHONY: all check-toolchain packages test help clean probe \
        flash-factory flash-boot flash-app-a \
        boot app_a app_b factory

# --------------------------------------------------------------------- 默认目标
all: check-toolchain $(BUILD)/bootloader.bin $(BUILD)/app_a.bin \
     $(BUILD)/app_b.bin $(BUILD)/factory.bin packages
	@echo ""
	@echo "Build complete (version $(VERSION)). Outputs in $(BUILD)/:"
	@echo "  bootloader.bin  app_a.bin  app_b.bin  factory.bin"
	@echo "  packages/manifest.json + app_a_v$(VERSION).fw + app_b_v$(VERSION).fw"

boot:    check-toolchain $(BUILD)/bootloader.bin
app_a:   check-toolchain $(BUILD)/app_a.bin
app_b:   check-toolchain $(BUILD)/app_b.bin
factory: check-toolchain $(BUILD)/factory.bin

check-toolchain:
	@$(CC) --version >/dev/null 2>&1 || { \
	    echo "ERROR: $(CC) not found."; \
	    echo "Install Arm GNU Toolchain and add its bin/ to PATH,"; \
	    echo "or run: make GCC_PATH=/path/to/toolchain/bin all"; \
	    echo "See docs/01_environment_setup.md"; exit 1; }

# ------------------------------------------------------------------ 编译规则
$(BUILD)/boot/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(BOOT_DEFS) $< -o $@

$(BUILD)/app_a/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(APPA_DEFS) $< -o $@

$(BUILD)/app_b/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(APPB_DEFS) $< -o $@

$(BUILD)/boot/obj/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) -c $(ASFLAGS) $(BOOT_DEFS) $< -o $@

$(BUILD)/app_a/obj/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) -c $(ASFLAGS) $(APPA_DEFS) $< -o $@

$(BUILD)/app_b/obj/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) -c $(ASFLAGS) $(APPB_DEFS) $< -o $@

# linker/app.ld 用 CPP 条件宏区分 A/B 地址, GNU ld 本身不认识 #ifndef
$(BUILD)/app_a/app.ld: linker/app.ld
	@mkdir -p $(dir $@)
	$(CC) -E -P -x c -DAPP_FLASH_ORIGIN=0x08005000 \
	      -DAPP_FLASH_LENGTH=0x0007B000 $< -o $@

$(BUILD)/app_b/app.ld: linker/app.ld
	@mkdir -p $(dir $@)
	$(CC) -E -P -x c -DAPP_FLASH_ORIGIN=0x08080000 \
	      -DAPP_FLASH_LENGTH=0x00080000 $< -o $@

# -------------------------------------------------------------------- 链接
$(BUILD)/boot/boot.elf: $(BOOT_OBJ) linker/boot.ld
	@mkdir -p $(dir $@)
	$(CC) $(filter %.o,$^) $(LDFLAGS) -T linker/boot.ld \
	      -Wl,-Map=$(BUILD)/boot/boot.map,--cref -o $@
	$(SZ) $@

$(BUILD)/app_a/app.elf: $(APPA_OBJ) $(BUILD)/app_a/app.ld
	@mkdir -p $(dir $@)
	$(CC) $(filter %.o,$^) $(LDFLAGS) -T $(BUILD)/app_a/app.ld \
	      -Wl,-Map=$(BUILD)/app_a/app.map,--cref -o $@
	$(SZ) $@

$(BUILD)/app_b/app.elf: $(APPB_OBJ) $(BUILD)/app_b/app.ld
	@mkdir -p $(dir $@)
	$(CC) $(filter %.o,$^) $(LDFLAGS) -T $(BUILD)/app_b/app.ld \
	      -Wl,-Map=$(BUILD)/app_b/app.map,--cref -o $@
	$(SZ) $@

$(BUILD)/bootloader.bin: $(BUILD)/boot/boot.elf
	@mkdir -p $(dir $@)
	$(CP) -O binary -S $< $@

$(BUILD)/app_a.bin: $(BUILD)/app_a/app.elf
	@mkdir -p $(dir $@)
	$(CP) -O binary -S $< $@

$(BUILD)/app_b.bin: $(BUILD)/app_b/app.elf
	@mkdir -p $(dir $@)
	$(CP) -O binary -S $< $@

# ------------------------------------------------------------- 出厂整片镜像
$(BUILD)/factory.bin: $(BUILD)/bootloader.bin $(BUILD)/app_a.bin tools/factory_image.py
	@mkdir -p $(dir $@)
	$(PY) tools/factory_image.py \
	    --boot $(BUILD)/bootloader.bin \
	    --app-a $(BUILD)/app_a.bin \
	    --version $(VERSION) --output $@

# --------------------------------------------------------------- OTA 升级包
packages: check-toolchain $(BUILD)/app_a.bin $(BUILD)/app_b.bin
	@mkdir -p $(BUILD)/packages
	$(PY) tools/fwpack.py pack \
	    --input $(BUILD)/app_a.bin \
	    --output $(BUILD)/packages/app_a_v$(VERSION).fw \
	    --slot A --version $(VERSION) \
	    --manifest $(BUILD)/packages/manifest.json
	$(PY) tools/fwpack.py pack \
	    --input $(BUILD)/app_b.bin \
	    --output $(BUILD)/packages/app_b_v$(VERSION).fw \
	    --slot B --version $(VERSION) \
	    --manifest $(BUILD)/packages/manifest.json
	$(PY) tools/fwpack.py verify \
	    --package $(BUILD)/packages/app_a_v$(VERSION).fw --slot A
	$(PY) tools/fwpack.py verify \
	    --package $(BUILD)/packages/app_b_v$(VERSION).fw --slot B

# ------------------------------------------------------------------ 主机测试
test:
	$(PY) -m unittest discover -s tests -v

# -------------------------------------------------------------------- 烧录
# 默认走 config/openocd_gd32f303.cfg（DAPLink + GD32F303 的 CPUTAPID=0x2ba01477）。
# 推荐用 `python tools/build.py flash-*`：它会自动定位 STM32CubeIDE 自带的
# OpenOCD、自动加 -s 脚本目录、自动截前 512 KiB，不需要手工配 PATH。
OPENOCD        ?= openocd
OPENOCD_CFG    ?= config/openocd_gd32f303.cfg
OPENOCD_SCRIPTS ?=
OPENOCD_BASE   := $(OPENOCD) $(if $(OPENOCD_SCRIPTS),-s $(OPENOCD_SCRIPTS),) -f $(OPENOCD_CFG)

# stm32f1x 驱动对 GD32 只认出 512 KiB，1 MiB 整片会越界；后半 Slot B 本就是空片
$(BUILD)/factory_512k.bin: $(BUILD)/factory.bin
	@$(PY) -c "from pathlib import Path; p=Path('$(BUILD)/factory.bin'); Path('$(BUILD)/factory_512k.bin').write_bytes(p.read_bytes()[:0x80000])"

flash-factory: $(BUILD)/factory_512k.bin
	$(OPENOCD_BASE) -c "program $(BUILD)/factory_512k.bin 0x08000000 verify reset exit"

flash-boot: $(BUILD)/bootloader.bin
	$(OPENOCD_BASE) -c "program $(BUILD)/bootloader.bin 0x08000000 verify reset exit"

flash-app-a: $(BUILD)/app_a.bin
	$(OPENOCD_BASE) -c "program $(BUILD)/app_a.bin 0x08005000 verify reset exit"

probe:
	$(OPENOCD_BASE) -c "init; targets; flash list; shutdown"

# ---------------------------------------------------------------------- 杂项
clean:
	rm -rf $(BUILD)

help:
	@echo "GD32F303VGT6 A/B OTA build targets"
	@echo "  make help                 this help"
	@echo "  make check-toolchain      verify arm-none-eabi-gcc is on PATH"
	@echo "  make all                  build all images + OTA packages"
	@echo "  make VERSION=1.1 all      build with App version 1.1"
	@echo "  make boot|app_a|app_b|factory   build one target"
	@echo "  make packages             repack .fw files + manifest.json"
	@echo "  make test                 host-side protocol/CRC/partition tests"
	@echo "  make probe                probe SWD connection (no erase/write)"
	@echo "  make flash-factory        flash first 512KiB of factory image via OpenOCD"
	@echo "  make flash-boot           flash bootloader only"
	@echo "  make flash-app-a          flash App-A only"
	@echo "  make clean                remove build/"
	@echo ""
	@echo "Overridable variables:"
	@echo "  GCC_PATH=/path/to/bin     PREFIX=arm-none-eabi-  PY=python"
	@echo "  VERSION=1.0               BUILD=build"
	@echo "  OPENOCD=...  OPENOCD_CFG=...  OPENOCD_SCRIPTS=..."
	@echo ""
	@echo "Prefer 'python tools/build.py ...' (auto-locates CubeIDE OpenOCD)."
	@echo ""
	@echo "Serial OTA (no debugger needed once Boot is flashed):"
	@echo "  python tools/serial_ota.py --port COM6 --slot B \\"
	@echo "      --package build/packages/app_b_v$(VERSION).fw --yes"

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
