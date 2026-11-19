#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

int boot_jump_to(uint32_t application_address, uint32_t application_size);

#endif
