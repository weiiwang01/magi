//
// Created by weiiwang01 on 10/20/26.
//

#ifndef HEADER_H
#define HEADER_H

#include <inttypes.h>

typedef struct __attribute__((packed))
{
  uint16_t patch_version;
  uint16_t minor_version;
  uint16_t major_version;
  uint16_t board_version;
  uint16_t reserved;
  char magic[4];
} header_firmware_t;

typedef struct __attribute__((packed))
{
  uint16_t patch_version;
  uint16_t minor_version;
  uint16_t major_version;
  uint16_t board_version;
  uint16_t reserved;
  char magic[4];
} header_bootloader_t;

#define HEADER_FIRMWARE                                                       \
  ((const volatile header_firmware_t *)(0x08078000U                           \
                                        - sizeof(header_firmware_t)))
#define HEADER_BOOTLOADER                                                     \
  ((const volatile header_bootloader_t *)(0x08010000U                         \
                                          - sizeof(header_bootloader_t)))

#endif // HEADER_H
