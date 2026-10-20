#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdbool.h>

bool bootloader_firmware_is_valid(void);
void bootloader_is_enabled(void);
void bootloader_exit(void);
void bootloader_process(void);

#endif
