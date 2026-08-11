#include "switch.h"

#include <stdint.h>

#include "board.h"
#include "rtk_types.h"
#include "port.h"
#include "rtk_error.h"
#include "rtk_switch.h"
#include "smi.h"
#include "stm32g0xx_hal.h"

#define SWITCH_SMI_DELAY_US 10U
#define SWITCH_RESET_ASSERT_MS 500U
#define SWITCH_RESET_STARTUP_MS 2000U
#define SWITCH_SGMII_PORT EXT_PORT0

static void
switch_smi_delay()
{
  const uint32_t cycles
      = (uint32_t)((((uint64_t)SystemCoreClock * SWITCH_SMI_DELAY_US)
                    + 999999U)
                   / 1000000U);
  uint32_t iterations = (cycles + 2U) / 3U;

  __asm volatile(".syntax unified\n"
                 "1:\n"
                 "subs %[iterations], %[iterations], #1\n"
                 "bne 1b\n"
                 : [iterations] "+l"(iterations)
                 :
                 : "cc");
}

static void
switch_gpio_set(GPIO_TypeDef *port, uint32_t pin, bool high)
{
  port->BSRR = high ? pin : (pin << 16U);
}

static bool
switch_gpio_get(GPIO_TypeDef *port, uint32_t pin)
{
  return (port->IDR & pin) != 0U;
}

static void
switch_gpio_output(GPIO_TypeDef *port, uint32_t pin)
{
  GPIO_InitTypeDef gpio_init = {
    .Pin = pin,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };

  HAL_GPIO_Init(port, &gpio_init);
}

static void
switch_gpio_input(GPIO_TypeDef *port, uint32_t pin)
{
  GPIO_InitTypeDef gpio_init = {
    .Pin = pin,
    .Mode = GPIO_MODE_INPUT,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };

  HAL_GPIO_Init(port, &gpio_init);
}

static void
switch_smi_scl_set(bool high)
{
  switch_gpio_set(
      BOARD_SWITCH_SMI_SCL_GPIO_PORT, BOARD_SWITCH_SMI_SCL_GPIO_PIN, high);
}

static void
switch_smi_sda_set(bool high)
{
  switch_gpio_set(
      BOARD_SWITCH_SMI_SDA_GPIO_PORT, BOARD_SWITCH_SMI_SDA_GPIO_PIN, high);
}

static void
switch_smi_sda_output()
{
  switch_gpio_output(BOARD_SWITCH_SMI_SDA_GPIO_PORT,
                     BOARD_SWITCH_SMI_SDA_GPIO_PIN);
}

static void
switch_smi_sda_input()
{
  switch_gpio_input(BOARD_SWITCH_SMI_SDA_GPIO_PORT,
                    BOARD_SWITCH_SMI_SDA_GPIO_PIN);
}

static void
switch_smi_start()
{
  switch_smi_scl_set(false);
  switch_smi_sda_set(true);
  switch_gpio_output(BOARD_SWITCH_SMI_SCL_GPIO_PORT,
                     BOARD_SWITCH_SMI_SCL_GPIO_PIN);
  switch_smi_sda_output();
  switch_smi_delay();

  switch_smi_scl_set(true);
  switch_smi_delay();
  switch_smi_scl_set(false);
  switch_smi_delay();

  switch_smi_scl_set(true);
  switch_smi_delay();
  switch_smi_sda_set(false);
  switch_smi_delay();
  switch_smi_scl_set(false);
  switch_smi_delay();
  switch_smi_sda_set(true);
}

static void
switch_smi_write_bits(uint16_t value, uint32_t bit_count)
{
  switch_smi_sda_output();

  while (bit_count > 0U)
    {
      switch_smi_delay();
      switch_smi_sda_set((value & (1U << (bit_count - 1U))) != 0U);
      switch_smi_delay();
      switch_smi_scl_set(true);
      switch_smi_delay();
      switch_smi_scl_set(false);
      --bit_count;
    }
}

static uint32_t
switch_smi_read_bits(uint32_t bit_count)
{
  uint32_t value = 0U;

  switch_smi_sda_input();

  while (bit_count > 0U)
    {
      switch_smi_delay();
      switch_smi_scl_set(true);
      switch_smi_delay();
      if (switch_gpio_get(BOARD_SWITCH_SMI_SDA_GPIO_PORT,
                          BOARD_SWITCH_SMI_SDA_GPIO_PIN))
        {
          value |= 1U << (bit_count - 1U);
        }
      switch_smi_scl_set(false);
      --bit_count;
    }

  return value;
}

static void
switch_smi_stop()
{
  switch_smi_sda_output();
  switch_smi_delay();
  switch_smi_sda_set(false);
  switch_smi_scl_set(true);
  switch_smi_delay();
  switch_smi_sda_set(true);
  switch_smi_delay();
  switch_smi_scl_set(true);
  switch_smi_delay();
  switch_smi_scl_set(false);
  switch_smi_delay();
  switch_smi_scl_set(true);
  switch_smi_delay();
  switch_smi_scl_set(false);
  switch_smi_delay();
  switch_smi_scl_set(true);

  switch_smi_sda_input();
  switch_gpio_input(BOARD_SWITCH_SMI_SCL_GPIO_PORT,
                    BOARD_SWITCH_SMI_SCL_GPIO_PIN);
}

static int
switch_smi_read_ack()
{
  for (uint32_t attempt = 0U; attempt < ack_timer; ++attempt)
    {
      if (switch_smi_read_bits(1U) == 0U)
        {
          return RT_ERR_OK;
        }
    }

  return RT_ERR_FAILED;
}

rtk_int32
smi_read(rtk_uint32 address, rtk_uint32 *data)
{
  rtk_uint32 low;
  rtk_uint32 high;
  int result = RT_ERR_OK;

  if (address > 0xffffU)
    {
      return RT_ERR_INPUT;
    }
  if (data == NULL)
    {
      return RT_ERR_NULL_POINTER;
    }

  switch_smi_start();
  switch_smi_write_bits(0x0bU, 4U);
  switch_smi_write_bits(0x04U, 3U);
  switch_smi_write_bits(0x01U, 1U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)address, 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)(address >> 8U), 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  low = switch_smi_read_bits(8U);
  switch_smi_write_bits(0U, 1U);
  high = switch_smi_read_bits(8U);
  switch_smi_write_bits(1U, 1U);
  switch_smi_stop();

  *data = low | (high << 8U);
  return result;
}

rtk_int32
smi_write(rtk_uint32 address, rtk_uint32 data)
{
  int result = RT_ERR_OK;

  if ((address > 0xffffU) || (data > 0xffffU))
    {
      return RT_ERR_INPUT;
    }

  switch_smi_start();
  switch_smi_write_bits(0x0bU, 4U);
  switch_smi_write_bits(0x04U, 3U);
  switch_smi_write_bits(0x00U, 1U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)address, 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)(address >> 8U), 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)data, 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_write_bits((uint16_t)(data >> 8U), 8U);
  if (switch_smi_read_ack() != RT_ERR_OK)
    {
      result = RT_ERR_FAILED;
    }

  switch_smi_stop();
  return result;
}

static void
switch_gpio_init()
{
  BOARD_SWITCH_RESET_GPIO_CLK_ENABLE();
  BOARD_SWITCH_SMI_SCL_GPIO_CLK_ENABLE();
  BOARD_SWITCH_SMI_SDA_GPIO_CLK_ENABLE();

  switch_gpio_set(
      BOARD_SWITCH_RESET_GPIO_PORT, BOARD_SWITCH_RESET_GPIO_PIN, false);
  switch_gpio_output(BOARD_SWITCH_RESET_GPIO_PORT,
                     BOARD_SWITCH_RESET_GPIO_PIN);
  switch_gpio_input(BOARD_SWITCH_SMI_SCL_GPIO_PORT,
                    BOARD_SWITCH_SMI_SCL_GPIO_PIN);
  switch_smi_sda_input();

  HAL_Delay(SWITCH_RESET_ASSERT_MS);
  switch_gpio_set(
      BOARD_SWITCH_RESET_GPIO_PORT, BOARD_SWITCH_RESET_GPIO_PIN, true);
  HAL_Delay(SWITCH_RESET_STARTUP_MS);
}

int
switch_init()
{
  switch_chip_t chip;
  rtk_api_ret_t result;

  switch_gpio_init();

  result = rtk_switch_probe(&chip);
  if (result != RT_ERR_OK)
    {
      return result;
    }
  if (chip != CHIP_RTL8367C)
    {
      return RT_ERR_CHIP_NOT_SUPPORTED;
    }

  result = rtk_switch_init();
  if (result != RT_ERR_OK)
    {
      return result;
    }

  return rtk_port_phyEnableAll_set(ENABLED);
}

int
switch_enable_sgmii(switch_protocol_t protocol,
                    switch_speed_t speed,
                    switch_duplex_t duplex)
{
  const bool auto_negotiation = speed == SWITCH_SPEED_AUTO;
  rtk_port_duplex_t port_duplex;
  rtk_port_mac_ability_t ability = {
    .forcemode = auto_negotiation ? 0U : 1U,
    .link = PORT_LINKUP,
    .nway = auto_negotiation ? 1U : 0U,
    .txpause = 1U,
    .rxpause = 1U,
  };
  rtk_mode_ext_t mode;
  rtk_api_ret_t result;

  switch (duplex)
    {
    case SWITCH_DUPLEX_HALF:
      port_duplex = PORT_HALF_DUPLEX;
      break;
    case SWITCH_DUPLEX_FULL:
      port_duplex = PORT_FULL_DUPLEX;
      break;
    default:
      return RT_ERR_INPUT;
    }

  switch (protocol)
    {
    case SWITCH_PROTOCOL_SGMII:
      mode = MODE_EXT_SGMII;
      break;
    case SWITCH_PROTOCOL_HSGMII:
      if ((speed != SWITCH_SPEED_2500M) || (duplex != SWITCH_DUPLEX_FULL))
        {
          return RT_ERR_INPUT;
        }
      mode = MODE_EXT_HSGMII;
      break;
    default:
      return RT_ERR_INPUT;
    }

  switch (speed)
    {
    case SWITCH_SPEED_AUTO:
      if (protocol == SWITCH_PROTOCOL_HSGMII)
        {
          ability.speed = PORT_SPEED_2500M;
        }
      else
        {
          ability.speed = PORT_SPEED_1000M;
        }
      break;
    case SWITCH_SPEED_10M:
      ability.speed = PORT_SPEED_10M;
      break;
    case SWITCH_SPEED_100M:
      ability.speed = PORT_SPEED_100M;
      break;
    case SWITCH_SPEED_1000M:
      ability.speed = PORT_SPEED_1000M;
      break;
    case SWITCH_SPEED_2500M:
      if (protocol != SWITCH_PROTOCOL_HSGMII)
        {
          return RT_ERR_INPUT;
        }
      ability.speed = PORT_SPEED_2500M;
      break;
    default:
      return RT_ERR_INPUT;
    }

  ability.duplex = port_duplex;

  result = rtk_port_macForceLinkExt_set(SWITCH_SGMII_PORT, mode, &ability);
  if (result != RT_ERR_OK)
    {
      return result;
    }

  result = rtk_port_sgmiiNway_set(SWITCH_SGMII_PORT,
                                  auto_negotiation ? ENABLED : DISABLED);
  if (result != RT_ERR_OK)
    {
      return result;
    }

  return rtk_port_sds_reset(SWITCH_SGMII_PORT);
}

int
switch_disable_sgmii()
{
  rtk_port_mac_ability_t ability = { 0 };
  rtk_api_ret_t result;

  result = rtk_port_sgmiiNway_set(SWITCH_SGMII_PORT, DISABLED);
  if (result != RT_ERR_OK)
    {
      return result;
    }

  result = rtk_port_macForceLinkExt_set(
      SWITCH_SGMII_PORT, MODE_EXT_DISABLE, &ability);
  if (result != RT_ERR_OK)
    {
      return result;
    }

  return rtk_port_sds_reset(SWITCH_SGMII_PORT);
}

int
switch_get_port_status(uint8_t port, switch_port_status_t *status)
{
  rtk_port_linkStatus_t link;
  rtk_port_speed_t speed;
  rtk_port_duplex_t duplex;
  switch_port_status_t port_status = { 0 };
  rtk_api_ret_t result;

  if (status == NULL)
    {
      return RT_ERR_NULL_POINTER;
    }

  result = rtk_port_phyStatus_get(port, &link, &speed, &duplex);
  if (result != RT_ERR_OK)
    {
      return result;
    }

  port_status.link = link == PORT_LINKUP;
  port_status.full_duplex = duplex == PORT_FULL_DUPLEX;

  switch (speed)
    {
    case PORT_SPEED_10M:
      port_status.speed = SWITCH_SPEED_10M;
      break;
    case PORT_SPEED_100M:
      port_status.speed = SWITCH_SPEED_100M;
      break;
    case PORT_SPEED_1000M:
      port_status.speed = SWITCH_SPEED_1000M;
      break;
    case PORT_SPEED_2500M:
      port_status.speed = SWITCH_SPEED_2500M;
      break;
    default:
      return RT_ERR_FAILED;
    }

  *status = port_status;
  return RT_ERR_OK;
}
