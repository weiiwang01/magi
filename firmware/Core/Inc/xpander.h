#ifndef XPANDER_H
#define XPANDER_H

#include <stdint.h>

void xpander_init();
int xpander_get_version(uint8_t *version);
int
xpander_i2c_write(uint8_t address, const uint8_t *data, unsigned int length);
int xpander_i2c_read(uint8_t address, uint8_t *data, unsigned int length);

#endif
