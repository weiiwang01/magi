#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdbool.h>

typedef enum {
    BOOTLOADER_ERROR_NONE = 0,
    BOOTLOADER_ERROR_INVALID_FIRMWARE = 1,
} bootloader_error_t;

bool bootloader_firmware_is_valid(void);
void bootloader_is_enabled(void);
void bootloader_exit(void);
void bootloader_process(void);

#endif
