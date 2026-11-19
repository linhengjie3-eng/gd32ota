#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdint.h>
#include "boot_metadata.h"

uint8_t boot_policy_select(const boot_metadata_t *metadata);

#endif
