#include "boot_policy.h"

static uint8_t other_slot(uint8_t slot)
{
    return (slot == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
}

uint8_t boot_policy_select(const boot_metadata_t *metadata)
{
    if ((metadata->pending_slot <= SLOT_ID_B) &&
        (metadata->slots[metadata->pending_slot].state == SLOT_STATE_PENDING) &&
        (metadata->trial_count < BOOT_MAX_TRIALS)) {
        return metadata->pending_slot;
    }
    if ((metadata->active_slot <= SLOT_ID_B) &&
        (metadata->slots[metadata->active_slot].state == SLOT_STATE_CONFIRMED)) {
        return metadata->active_slot;
    }
    if (metadata->active_slot <= SLOT_ID_B) {
        uint8_t fallback = other_slot(metadata->active_slot);
        if (metadata->slots[fallback].state == SLOT_STATE_CONFIRMED) {
            return fallback;
        }
    }
    if (metadata->slots[SLOT_ID_A].state == SLOT_STATE_CONFIRMED) {
        return SLOT_ID_A;
    }
    if (metadata->slots[SLOT_ID_B].state == SLOT_STATE_CONFIRMED) {
        return SLOT_ID_B;
    }
    return SLOT_ID_NONE;
}
