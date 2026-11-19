#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdbool.h>
#include <stdint.h>
#include "memory_map.h"
#include "firmware_header.h"

#define BOOT_META_MAGIC             0x4D455441u
#define BOOT_META_FORMAT_VERSION    1u
#define BOOT_MAX_TRIALS             3u

typedef enum {
    SLOT_STATE_EMPTY = 0,
    SLOT_STATE_CONFIRMED = 1,
    SLOT_STATE_PENDING = 2,
    SLOT_STATE_INVALID = 3
} slot_state_t;

typedef struct {
    uint32_t size;
    uint32_t crc32;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t build_time;
    uint8_t state;
    uint8_t reserved[3];
} slot_info_t;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t length;
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t request_upgrade;
    uint8_t trial_count;
    slot_info_t slots[2];
    uint32_t crc32;
} boot_metadata_t;

void boot_metadata_defaults(boot_metadata_t *metadata);
bool boot_metadata_sanitize(boot_metadata_t *metadata);
bool boot_metadata_load(boot_metadata_t *metadata);
bool boot_metadata_save(const boot_metadata_t *metadata);
bool boot_metadata_request_upgrade(void);
bool boot_metadata_request_rollback(uint8_t running_slot,
                                    uint16_t running_major,
                                    uint16_t running_minor);
bool boot_metadata_confirm_running(uint8_t running_slot);
uint8_t boot_metadata_choose_download_slot(const boot_metadata_t *metadata);
bool boot_image_layout_valid(uint8_t slot, const FirmwareHeader_t *header);
bool boot_image_validate(uint8_t slot, const slot_info_t *info);

#endif
