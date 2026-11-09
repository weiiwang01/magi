#include "usb_hub.h"

#include <stdint.h>

#include "bitbang_i2c.h"
#include "board.h"

#define USB2514B_I2C_ADDRESS 0x2cU
#define USB2514B_I2C_TIMEOUT_MS 100U

#define USB2514B_CONFIG_BYTE_3_REGISTER 0x08U
#define USB2514B_PORT_MAPPING_ENABLE (1U << 3U)
#define USB2514B_PORT_SWAP_REGISTER 0xfaU
#define USB2514B_PORT_MAP_12_REGISTER 0xfbU
#define USB2514B_STATUS_COMMAND_REGISTER 0xffU
#define USB2514B_USB_ATTACH (1U << 0U)

int
usb_hub_init()
{
  bitbang_i2c_t i2c = {
    .scl_port = BOARD_USB_HUB_I2C_SCL_GPIO_PORT,
    .scl_pin = BOARD_USB_HUB_I2C_SCL_GPIO_PIN,
    .sda_port = BOARD_USB_HUB_I2C_SDA_GPIO_PORT,
    .sda_pin = BOARD_USB_HUB_I2C_SDA_GPIO_PIN,
  };
  uint8_t port_swap[] = {
    USB2514B_PORT_SWAP_REGISTER,
    1U,
    BOARD_USB_HUB_PORT_SWAP,
  };
  uint8_t port_map[] = {
    USB2514B_PORT_MAP_12_REGISTER,
    2U,
    BOARD_USB_HUB_PORT_MAP_12,
    BOARD_USB_HUB_PORT_MAP_34,
  };
  uint8_t config_byte_3[] = {
    USB2514B_CONFIG_BYTE_3_REGISTER,
    1U,
    USB2514B_PORT_MAPPING_ENABLE,
  };
  uint8_t attach[] = {
    USB2514B_STATUS_COMMAND_REGISTER,
    1U,
    USB2514B_USB_ATTACH,
  };
  int result;

  result = bitbang_i2c_init(&i2c, BITBANG_I2C_SPEED_STANDARD);
  if (result != 0)
    {
      return result;
    }

  result = bitbang_i2c_master_transmit(&i2c,
                                       USB2514B_I2C_ADDRESS,
                                       port_swap,
                                       sizeof(port_swap),
                                       USB2514B_I2C_TIMEOUT_MS);
  if (result != 0)
    {
      return result;
    }

  result = bitbang_i2c_master_transmit(&i2c,
                                       USB2514B_I2C_ADDRESS,
                                       port_map,
                                       sizeof(port_map),
                                       USB2514B_I2C_TIMEOUT_MS);
  if (result != 0)
    {
      return result;
    }

  result = bitbang_i2c_master_transmit(&i2c,
                                       USB2514B_I2C_ADDRESS,
                                       config_byte_3,
                                       sizeof(config_byte_3),
                                       USB2514B_I2C_TIMEOUT_MS);
  if (result != 0)
    {
      return result;
    }

  return bitbang_i2c_master_transmit(&i2c,
                                     USB2514B_I2C_ADDRESS,
                                     attach,
                                     sizeof(attach),
                                     USB2514B_I2C_TIMEOUT_MS);
}
