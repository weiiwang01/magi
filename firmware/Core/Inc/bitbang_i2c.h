#ifndef BITBANG_I2C_H
#define BITBANG_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "stm32g0b1xx.h"

typedef enum
{
  BITBANG_I2C_SPEED_STANDARD = 100000U,
  BITBANG_I2C_SPEED_FAST = 400000U,
} bitbang_i2c_speed_t;

typedef struct
{
  GPIO_TypeDef *scl_port;
  uint32_t scl_pin;
  GPIO_TypeDef *sda_port;
  uint32_t sda_pin;
  bitbang_i2c_speed_t speed;
  uint32_t delays[2];
} bitbang_i2c_t;

int bitbang_i2c_init(bitbang_i2c_t *i2c, bitbang_i2c_speed_t speed);
int bitbang_i2c_master_transmit(bitbang_i2c_t *i2c,
                                uint8_t address,
                                uint8_t *data,
                                size_t len,
                                uint32_t timeout);
int bitbang_i2c_master_receive(bitbang_i2c_t *i2c,
                               uint8_t address,
                               uint8_t *data,
                               size_t len,
                               uint32_t timeout);

#endif /* BITBANG_I2C_H */
