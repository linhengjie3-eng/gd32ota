#include "boot_metadata.h"
#include <stddef.h>
#include <string.h>
#include "crc.h"
#include "bsp_flash.h"

static const uint32_t meta_pages[2] = {META_PAGE_A_ADDR, META_PAGE_B_ADDR};

void boot_metadata_defaults(boot_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = BOOT_META_MAGIC;
    metadata->format_version = BOOT_META_FORMAT_VERSION;
    metadata->length = sizeof(*metadata);
    metadata->active_slot = SLOT_ID_A;
    metadata->pending_slot = SLOT_ID_NONE;
    metadata->request_upgrade = 0;
    metadata->trial_count = 0;
}

static bool metadata_crc_valid(const boot_metadata_t *metadata)
{
    uint32_t expected;
    if ((metadata->magic != BOOT_META_MAGIC) ||
        (metadata->format_version != BOOT_META_FORMAT_VERSION) ||
        (metadata->length != sizeof(*metadata))) {
        return false;
    }
    expected = crc32_compute((const uint8_t *)metadata,
                             offsetof(boot_metadata_t, crc32));
    return expected == metadata->crc32;
}

bool boot_metadata_sanitize(boot_metadata_t *metadata)
{
    unsigned i;
    if (metadata->active_slot > SLOT_ID_B) {
        return false;
    }
    if ((metadata->pending_slot > SLOT_ID_B) &&
        (metadata->pending_slot != SLOT_ID_NONE)) {
        return false;
    }
    for (i = 0; i < 2; ++i) {
        if ((metadata->slots[i].state == SLOT_STATE_EMPTY) ||
            (metadata->slots[i].state == SLOT_STATE_INVALID)) {
            continue;
        }
        if ((metadata->slots[i].state != SLOT_STATE_CONFIRMED) &&
            (metadata->slots[i].state != SLOT_STATE_PENDING)) {
            return false;
        }
        if ((metadata->slots[i].size == 0) ||
            (metadata->slots[i].size > slot_capacity((uint8_t)i))) {
            return false;
        }
    }
    return true;
}

bool boot_metadata_load(boot_metadata_t *metadata)
{
    boot_metadata_t candidate[2];
    bool valid[2];
    unsigned i;
    int newest = -1;

    for (i = 0; i < 2; ++i) {
        memcpy(&candidate[i], (const void *)meta_pages[i], sizeof(candidate[i]));
        valid[i] = metadata_crc_valid(&candidate[i]) && boot_metadata_sanitize(&candidate[i]);
        if (valid[i]) {
            if ((newest < 0) ||
                ((int32_t)(candidate[i].sequence - candidate[newest].sequence) > 0)) {
                newest = (int)i;
            }
        }
    }
    if (newest < 0) {
        boot_metadata_defaults(metadata);
        return false;
    }
    memcpy(metadata, &candidate[newest], sizeof(*metadata));
    return true;
}

bool boot_metadata_save(const boot_metadata_t *metadata)
{
    boot_metadata_t current;
    boot_metadata_t next = *metadata;
    uint32_t target = META_PAGE_A_ADDR;
    bool current_valid = boot_metadata_load(&current);
    bool result;

    if (current_valid) {
        const boot_metadata_t *a = (const boot_metadata_t *)META_PAGE_A_ADDR;
        const boot_metadata_t *b = (const boot_metadata_t *)META_PAGE_B_ADDR;
        bool a_valid = metadata_crc_valid(a) && boot_metadata_sanitize((boot_metadata_t *)a);
        bool b_valid = metadata_crc_valid(b) && boot_metadata_sanitize((boot_metadata_t *)b);
        if (a_valid && (!b_valid ||
            ((int32_t)(a->sequence - b->sequence) >= 0))) {
            target = META_PAGE_B_ADDR;
        } else {
            target = META_PAGE_A_ADDR;
        }
        next.sequence = current.sequence + 1u;
    } else {
        next.sequence = 1u;
    }
    next.magic = BOOT_META_MAGIC;
    next.format_version = BOOT_META_FORMAT_VERSION;
    next.length = sizeof(next);
    next.crc32 = 0;
    next.crc32 = crc32_compute((const uint8_t *)&next, offsetof(boot_metadata_t, crc32));

    if (!boot_metadata_sanitize(&next)) {
        return false;
    }
    result = bsp_flash_erase(target, META_PAGE_BYTES) &&
             bsp_flash_write(target, &next, sizeof(next));
    if (!result) {
        return false;
    }
    {
        boot_metadata_t verify;
        memcpy(&verify, (const void *)target, sizeof(verify));
        return metadata_crc_valid(&verify) &&
               (verify.sequence == next.sequence) &&
               (memcmp(&verify, &next, sizeof(next)) == 0);
    }
}

bool boot_metadata_request_upgrade(void)
{
    boot_metadata_t metadata;
    boot_metadata_load(&metadata);
    metadata.request_upgrade = 1;
    return boot_metadata_save(&metadata);
}

static bool version_is_older(uint16_t major, uint16_t minor,
                             uint16_t ref_major, uint16_t ref_minor)
{
    if (major != ref_major) {
        return major < ref_major;
    }
    return minor < ref_minor;
}

/* 回退到备份槽：只有备份槽已确认、镜像自检通过且版本严格更低时才允许。
   成功后 Boot 下次启动会直接跳备份槽（active_slot 已指向它）。 */
bool boot_metadata_request_rollback(uint8_t running_slot,
                                    uint16_t running_major,
                                    uint16_t running_minor)
{
    boot_metadata_t metadata;
    uint8_t backup;

    if (running_slot > SLOT_ID_B) {
        return false;
    }
    if (!boot_metadata_load(&metadata)) {
        return false;
    }
    backup = (running_slot == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
    if (metadata.slots[backup].state != SLOT_STATE_CONFIRMED) {
        return false;
    }
    if (!version_is_older(metadata.slots[backup].version_major,
                          metadata.slots[backup].version_minor,
                          running_major, running_minor)) {
        return false;
    }
    if (!boot_image_validate(backup, &metadata.slots[backup])) {
        return false;
    }

    metadata.active_slot = backup;
    metadata.pending_slot = SLOT_ID_NONE;
    metadata.request_upgrade = 0;
    metadata.trial_count = 0;
    return boot_metadata_save(&metadata);
}

bool boot_metadata_confirm_running(uint8_t running_slot)
{
    boot_metadata_t metadata;
    boot_metadata_load(&metadata);
    if (running_slot > SLOT_ID_B) {
        return false;
    }
    if (metadata.slots[running_slot].state == SLOT_STATE_CONFIRMED &&
        metadata.active_slot == running_slot &&
        metadata.pending_slot == SLOT_ID_NONE &&
        metadata.trial_count == 0) {
        return true;
    }
    metadata.active_slot = running_slot;
    metadata.pending_slot = SLOT_ID_NONE;
    metadata.request_upgrade = 0;
    metadata.trial_count = 0;
    metadata.slots[running_slot].state = SLOT_STATE_CONFIRMED;
    return boot_metadata_save(&metadata);
}

uint8_t boot_metadata_choose_download_slot(const boot_metadata_t *metadata)
{
    if (metadata->active_slot <= SLOT_ID_B &&
        metadata->slots[metadata->active_slot].state == SLOT_STATE_CONFIRMED) {
        return (metadata->active_slot == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
    }
    if (metadata->slots[SLOT_ID_A].state != SLOT_STATE_CONFIRMED) {
        return SLOT_ID_A;
    }
    return SLOT_ID_B;
}

bool boot_image_layout_valid(uint8_t slot, const FirmwareHeader_t *header)
{
    return (slot <= SLOT_ID_B) && (header->magic == FW_MAGIC) &&
           (header->firmware_size >= 8u) &&
           (header->firmware_size <= slot_capacity(slot));
}

bool boot_image_validate(uint8_t slot, const slot_info_t *info)
{
    uint32_t base;
    uint32_t stack;
    uint32_t reset;
    uint32_t crc;

    if ((slot > SLOT_ID_B) || (info->size < 8u) ||
        (info->size > slot_capacity(slot)) ||
        ((info->state != SLOT_STATE_CONFIRMED) &&
         (info->state != SLOT_STATE_PENDING))) {
        return false;
    }
    base = slot_base_address(slot);
    memcpy(&stack, (const void *)base, sizeof(stack));
    memcpy(&reset, (const void *)(base + 4u), sizeof(reset));
    if ((stack < SRAM_BASE_ADDR) || (stack > SRAM_END_ADDR) ||
        ((stack & 0x7u) != 0u) || ((reset & 1u) == 0u)) {
        return false;
    }
    reset &= ~1u;
    if ((reset < base) || (reset >= (base + info->size))) {
        return false;
    }
    crc = crc32_compute((const uint8_t *)base, info->size);
    return crc == info->crc32;
}
