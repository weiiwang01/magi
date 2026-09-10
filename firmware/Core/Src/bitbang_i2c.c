/*
 * Ported from Zephyr's drivers/i2c/i2c_bitbang.c.
 */

#include "bitbang_i2c.h"

#include <errno.h>
#include <stdbool.h>

#include "stm32g0xx_hal.h"
#include "timeout.h"

#define T_LOW 0U
#define T_HIGH 1U
#define T_SU_STA T_LOW
#define T_HD_STA T_HIGH
#define T_SU_STP T_HIGH
#define T_BUF T_LOW

static int
gpio_clock_enable(GPIO_TypeDef *port)
{
#if defined(GPIOA)
  if (port == GPIOA)
    {
      __HAL_RCC_GPIOA_CLK_ENABLE();
      return 0;
    }
#endif
#if defined(GPIOB)
  if (port == GPIOB)
    {
      __HAL_RCC_GPIOB_CLK_ENABLE();
      return 0;
    }
#endif
#if defined(GPIOC)
  if (port == GPIOC)
    {
      __HAL_RCC_GPIOC_CLK_ENABLE();
      return 0;
    }
#endif
#if defined(GPIOD)
  if (port == GPIOD)
    {
      __HAL_RCC_GPIOD_CLK_ENABLE();
      return 0;
    }
#endif
#if defined(GPIOE)
  if (port == GPIOE)
    {
      __HAL_RCC_GPIOE_CLK_ENABLE();
      return 0;
    }
#endif
#if defined(GPIOF)
  if (port == GPIOF)
    {
      __HAL_RCC_GPIOF_CLK_ENABLE();
      return 0;
    }
#endif
  return -EINVAL;
}

static bool
gpio_pin_is_valid(uint32_t pin)
{
  return (pin != 0U) && ((pin & (pin - 1U)) == 0U)
         && ((pin & ~GPIO_PIN_All) == 0U);
}

static void
gpio_set(GPIO_TypeDef *port, uint32_t pin, bool state)
{
  port->BSRR = state ? pin : (pin << 16U);
}

static bool
gpio_get(GPIO_TypeDef *port, uint32_t pin)
{
  return (port->IDR & pin) != 0U;
}

static int
i2c_set_scl(bitbang_i2c_t *i2c, bool state, const timeout_t *timeout)
{
  gpio_set(i2c->scl_port, i2c->scl_pin, state);

  if (state)
    {
      while (!gpio_get(i2c->scl_port, i2c->scl_pin))
        {
          if (timeout_expired(timeout))
            {
              return -ETIMEDOUT;
            }
        }
    }

  return 0;
}

static void
i2c_set_sda(bitbang_i2c_t *i2c, bool state)
{
  gpio_set(i2c->sda_port, i2c->sda_pin, state);
}

static bool
i2c_get_sda(bitbang_i2c_t *i2c)
{
  return gpio_get(i2c->sda_port, i2c->sda_pin);
}

static int
i2c_start(bitbang_i2c_t *i2c, const timeout_t *timeout)
{
  int result;

  if (!i2c_get_sda(i2c))
    {
      (void)i2c_set_scl(i2c, false, timeout);
      timeout_sleep_cycles(i2c->delays[T_LOW]);
      result = i2c_set_scl(i2c, true, timeout);
      if (result != 0)
        {
          return result;
        }
      timeout_sleep_cycles(i2c->delays[T_SU_STA]);
    }

  i2c_set_sda(i2c, false);
  timeout_sleep_cycles(i2c->delays[T_HD_STA]);

  (void)i2c_set_scl(i2c, false, timeout);
  timeout_sleep_cycles(i2c->delays[T_LOW]);
  return timeout_expired(timeout) ? -ETIMEDOUT : 0;
}

static int
i2c_stop(bitbang_i2c_t *i2c, const timeout_t *timeout)
{
  int result;

  i2c_set_sda(i2c, false);
  timeout_sleep_cycles(i2c->delays[T_LOW]);

  result = i2c_set_scl(i2c, true, timeout);
  if (result != 0)
    {
      return result;
    }
  timeout_sleep_cycles(i2c->delays[T_HIGH]);

  timeout_sleep_cycles(i2c->delays[T_SU_STP]);
  i2c_set_sda(i2c, true);
  timeout_sleep_cycles(i2c->delays[T_BUF]);
  return timeout_expired(timeout) ? -ETIMEDOUT : 0;
}

static int
i2c_write_bit(bitbang_i2c_t *i2c, bool bit, const timeout_t *timeout)
{
  int result;

  i2c_set_sda(i2c, bit);
  result = i2c_set_scl(i2c, true, timeout);
  if (result != 0)
    {
      return result;
    }
  timeout_sleep_cycles(i2c->delays[T_HIGH]);
  (void)i2c_set_scl(i2c, false, timeout);
  timeout_sleep_cycles(i2c->delays[T_LOW]);
  return timeout_expired(timeout) ? -ETIMEDOUT : 0;
}

static int
i2c_read_bit(bitbang_i2c_t *i2c, bool *bit, const timeout_t *timeout)
{
  int result;

  i2c_set_sda(i2c, true);
  result = i2c_set_scl(i2c, true, timeout);
  if (result != 0)
    {
      return result;
    }
  timeout_sleep_cycles(i2c->delays[T_HIGH]);

  *bit = i2c_get_sda(i2c);

  (void)i2c_set_scl(i2c, false, timeout);
  timeout_sleep_cycles(i2c->delays[T_LOW]);
  return timeout_expired(timeout) ? -ETIMEDOUT : 0;
}

static int
i2c_write_byte(bitbang_i2c_t *i2c, uint8_t byte, const timeout_t *timeout)
{
  uint8_t mask = 1U << 7;
  bool nack;
  int result;

  do
    {
      result = i2c_write_bit(i2c, (byte & mask) != 0U, timeout);
      if (result != 0)
        {
          return result;
        }
      mask >>= 1;
    }
  while (mask != 0U);

  result = i2c_read_bit(i2c, &nack, timeout);
  if (result != 0)
    {
      return result;
    }
  return nack ? -EIO : 0;
}

static int
i2c_read_byte(bitbang_i2c_t *i2c, uint8_t *byte, const timeout_t *timeout)
{
  unsigned int value = 1U;
  bool bit;
  int result;

  do
    {
      value <<= 1;
      result = i2c_read_bit(i2c, &bit, timeout);
      if (result != 0)
        {
          return result;
        }
      value |= bit;
    }
  while ((value & (1U << 8)) == 0U);

  *byte = (uint8_t)value;
  return 0;
}

static int
i2c_transfer(bitbang_i2c_t *i2c,
             uint8_t address,
             uint8_t *data,
             size_t len,
             bool read,
             uint32_t timeout_ms)
{
  timeout_t timeout;
  timeout_init(&timeout, timeout_ms);
  int result;
  int stop_result;

  if ((i2c == NULL) || (address > 0x7fU) || ((data == NULL) && (len != 0U))
      || ((i2c->speed != BITBANG_I2C_SPEED_STANDARD)
          && (i2c->speed != BITBANG_I2C_SPEED_FAST)))
    {
      return -EINVAL;
    }

  result = i2c_set_scl(i2c, true, &timeout);
  if (result == 0)
    {
      result = i2c_stop(i2c, &timeout);
    }
  if (result == 0)
    {
      result = i2c_start(i2c, &timeout);
    }
  if (result == 0)
    {
      result
          = i2c_write_byte(i2c, (uint8_t)((address << 1U) | read), &timeout);
    }

  for (size_t index = 0U; (result == 0) && (index < len); ++index)
    {
      if (read)
        {
          result = i2c_read_byte(i2c, &data[index], &timeout);
          if (result == 0)
            {
              result = i2c_write_bit(i2c, (index + 1U) == len, &timeout);
            }
        }
      else
        {
          result = i2c_write_byte(i2c, data[index], &timeout);
        }
    }

  stop_result = i2c_stop(i2c, &timeout);
  if (stop_result != 0)
    {
      i2c_set_sda(i2c, true);
      gpio_set(i2c->scl_port, i2c->scl_pin, true);
      if (result == 0)
        {
          result = stop_result;
        }
    }

  return result;
}

int
bitbang_i2c_init(bitbang_i2c_t *i2c, bitbang_i2c_speed_t speed)
{
  GPIO_InitTypeDef gpio_init = { 0 };

  if ((i2c == NULL) || (i2c->scl_port == NULL) || (i2c->sda_port == NULL)
      || !gpio_pin_is_valid(i2c->scl_pin) || !gpio_pin_is_valid(i2c->sda_pin)
      || ((i2c->scl_port == i2c->sda_port) && (i2c->scl_pin == i2c->sda_pin)))
    {
      return -EINVAL;
    }

  switch (speed)
    {
    case BITBANG_I2C_SPEED_STANDARD:
      i2c->delays[T_LOW] = TIMEOUT_NANOSECONDS_TO_CYCLES(4700U);
      i2c->delays[T_HIGH] = TIMEOUT_NANOSECONDS_TO_CYCLES(4000U);
      break;
    case BITBANG_I2C_SPEED_FAST:
      i2c->delays[T_LOW] = TIMEOUT_NANOSECONDS_TO_CYCLES(1300U);
      i2c->delays[T_HIGH] = TIMEOUT_NANOSECONDS_TO_CYCLES(600U);
      break;
    default:
      return -ENOTSUP;
    }

  if ((gpio_clock_enable(i2c->scl_port) != 0)
      || (gpio_clock_enable(i2c->sda_port) != 0))
    {
      return -EINVAL;
    }

  gpio_set(i2c->scl_port, i2c->scl_pin, true);
  gpio_set(i2c->sda_port, i2c->sda_pin, true);

  gpio_init.Pin = i2c->scl_pin;
  gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
  gpio_init.Pull = GPIO_PULLUP;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(i2c->scl_port, &gpio_init);

  gpio_init.Pin = i2c->sda_pin;
  HAL_GPIO_Init(i2c->sda_port, &gpio_init);

  i2c->speed = speed;
  return 0;
}

int
bitbang_i2c_master_transmit(bitbang_i2c_t *i2c,
                            uint8_t address,
                            uint8_t *data,
                            size_t len,
                            uint32_t timeout)
{
  return i2c_transfer(i2c, address, data, len, false, timeout);
}

int
bitbang_i2c_master_receive(bitbang_i2c_t *i2c,
                           uint8_t address,
                           uint8_t *data,
                           size_t len,
                           uint32_t timeout)
{
  return i2c_transfer(i2c, address, data, len, true, timeout);
}
