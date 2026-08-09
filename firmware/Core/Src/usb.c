#include "usb.h"

#include "board.h"

typedef enum
{
  USB_GPIO_LOW,
  USB_GPIO_HIGH,
  USB_GPIO_ANALOG,
} usb_gpio_level_t;

static bool usb_initialized;

static void
usb_gpio_set_level(GPIO_TypeDef *port, uint16_t pin, usb_gpio_level_t level)
{
  GPIO_InitTypeDef gpio_init = { 0 };

  gpio_init.Pin = pin;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

  if (level == USB_GPIO_ANALOG)
    {
      gpio_init.Mode = GPIO_MODE_ANALOG;
    }
  else
    {
      HAL_GPIO_WritePin(
          port, pin, (level == USB_GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
      gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    }

  HAL_GPIO_Init(port, &gpio_init);
}

static void
usb_init()
{
  if (usb_initialized)
    {
      return;
    }

  BOARD_USB0_POWER_5V_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB0_VBUS_DISCHARGE_GPIO_CLK_ENABLE();
  BOARD_USB0_DATA_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB0_DATA_POL_GPIO_CLK_ENABLE();
  BOARD_USB1_POWER_5V_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB1_POWER_20V_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB1_VBUS_DISCHARGE_GPIO_CLK_ENABLE();
  BOARD_USB1_HS_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB1_SS_ENABLE_GPIO_CLK_ENABLE();
  BOARD_USB1_SS_POL_GPIO_CLK_ENABLE();
  BOARD_USB1_SS_AMSEL_GPIO_CLK_ENABLE();
  BOARD_USB1_SS_EN_GPIO_CLK_ENABLE();

  usb_gpio_set_level(BOARD_USB0_POWER_5V_ENABLE_GPIO_PORT,
                     BOARD_USB0_POWER_5V_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB0_VBUS_DISCHARGE_GPIO_PORT,
                     BOARD_USB0_VBUS_DISCHARGE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB0_DATA_ENABLE_GPIO_PORT,
                     BOARD_USB0_DATA_ENABLE_GPIO_PIN,
                     USB_GPIO_HIGH);
  usb_gpio_set_level(BOARD_USB0_DATA_POL_GPIO_PORT,
                     BOARD_USB0_DATA_POL_GPIO_PIN,
                     USB_GPIO_LOW);

  usb_gpio_set_level(BOARD_USB1_POWER_5V_ENABLE_GPIO_PORT,
                     BOARD_USB1_POWER_5V_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB1_POWER_20V_ENABLE_GPIO_PORT,
                     BOARD_USB1_POWER_20V_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB1_VBUS_DISCHARGE_GPIO_PORT,
                     BOARD_USB1_VBUS_DISCHARGE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB1_HS_ENABLE_GPIO_PORT,
                     BOARD_USB1_HS_ENABLE_GPIO_PIN,
                     USB_GPIO_HIGH);
  usb_gpio_set_level(BOARD_USB1_SS_ENABLE_GPIO_PORT,
                     BOARD_USB1_SS_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(
      BOARD_USB1_SS_POL_GPIO_PORT, BOARD_USB1_SS_POL_GPIO_PIN, USB_GPIO_LOW);
  usb_gpio_set_level(BOARD_USB1_SS_AMSEL_GPIO_PORT,
                     BOARD_USB1_SS_AMSEL_GPIO_PIN,
                     USB_GPIO_ANALOG);
  usb_gpio_set_level(
      BOARD_USB1_SS_EN_GPIO_PORT, BOARD_USB1_SS_EN_GPIO_PIN, USB_GPIO_LOW);

  usb_initialized = true;
}

static int
usb0_set_data(usb_data_t hs, usb_data_t ss1, usb_data_t ss2)
{
  usb_gpio_level_t polarity;

  if ((hs == USB_DATA_NONE) && (ss1 == USB_DATA_NONE)
      && (ss2 == USB_DATA_NONE))
    {
      usb_init();
      usb_gpio_set_level(BOARD_USB0_DATA_ENABLE_GPIO_PORT,
                         BOARD_USB0_DATA_ENABLE_GPIO_PIN,
                         USB_GPIO_HIGH);
      usb_gpio_set_level(BOARD_USB0_DATA_POL_GPIO_PORT,
                         BOARD_USB0_DATA_POL_GPIO_PIN,
                         USB_GPIO_LOW);
      return USB_SUCCESS;
    }

  if (hs != USB_DATA_USB)
    {
      return USB_ERROR;
    }

  if ((ss1 == USB_DATA_USB) && (ss2 == USB_DATA_NONE))
    {
      polarity = USB_GPIO_LOW;
    }
  else if ((ss1 == USB_DATA_NONE) && (ss2 == USB_DATA_USB))
    {
      polarity = USB_GPIO_HIGH;
    }
  else
    {
      return USB_ERROR;
    }

  usb_init();
  usb_gpio_set_level(BOARD_USB0_DATA_ENABLE_GPIO_PORT,
                     BOARD_USB0_DATA_ENABLE_GPIO_PIN,
                     USB_GPIO_HIGH);
  usb_gpio_set_level(
      BOARD_USB0_DATA_POL_GPIO_PORT, BOARD_USB0_DATA_POL_GPIO_PIN, polarity);
  usb_gpio_set_level(BOARD_USB0_DATA_ENABLE_GPIO_PORT,
                     BOARD_USB0_DATA_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  return USB_SUCCESS;
}

static int
usb1_set_data(usb_data_t hs, usb_data_t ss1, usb_data_t ss2)
{
  usb_gpio_level_t hs_enable;
  usb_gpio_level_t ss_enable;
  usb_gpio_level_t polarity;
  usb_gpio_level_t amsel;

  switch (hs)
    {
    case USB_DATA_NONE:
      hs_enable = USB_GPIO_HIGH;
      break;
    case USB_DATA_USB:
      hs_enable = USB_GPIO_LOW;
      break;
    default:
      return USB_ERROR;
    }

  if ((ss1 == USB_DATA_NONE) && (ss2 == USB_DATA_NONE))
    {
      ss_enable = USB_GPIO_LOW;
      polarity = USB_GPIO_LOW;
      amsel = USB_GPIO_ANALOG;
    }
  else if ((ss1 == USB_DATA_USB) && (ss2 == USB_DATA_NONE))
    {
      ss_enable = USB_GPIO_HIGH;
      polarity = USB_GPIO_LOW;
      amsel = USB_GPIO_ANALOG;
    }
  else if ((ss1 == USB_DATA_NONE) && (ss2 == USB_DATA_USB))
    {
      ss_enable = USB_GPIO_HIGH;
      polarity = USB_GPIO_HIGH;
      amsel = USB_GPIO_ANALOG;
    }
  else if ((ss1 == USB_DATA_USB) && (ss2 == USB_DATA_ALT_MODE))
    {
      ss_enable = USB_GPIO_HIGH;
      polarity = USB_GPIO_LOW;
      amsel = USB_GPIO_LOW;
    }
  else if ((ss1 == USB_DATA_ALT_MODE) && (ss2 == USB_DATA_USB))
    {
      ss_enable = USB_GPIO_HIGH;
      polarity = USB_GPIO_HIGH;
      amsel = USB_GPIO_LOW;
    }
  else
    {
      return USB_ERROR;
    }

  usb_init();
  usb_gpio_set_level(BOARD_USB1_HS_ENABLE_GPIO_PORT,
                     BOARD_USB1_HS_ENABLE_GPIO_PIN,
                     hs_enable);

  usb_gpio_set_level(BOARD_USB1_SS_ENABLE_GPIO_PORT,
                     BOARD_USB1_SS_ENABLE_GPIO_PIN,
                     USB_GPIO_LOW);
  usb_gpio_set_level(
      BOARD_USB1_SS_EN_GPIO_PORT, BOARD_USB1_SS_EN_GPIO_PIN, USB_GPIO_LOW);
  usb_gpio_set_level(
      BOARD_USB1_SS_POL_GPIO_PORT, BOARD_USB1_SS_POL_GPIO_PIN, polarity);
  usb_gpio_set_level(
      BOARD_USB1_SS_AMSEL_GPIO_PORT, BOARD_USB1_SS_AMSEL_GPIO_PIN, amsel);

  if (ss_enable == USB_GPIO_HIGH)
    {
      usb_gpio_set_level(BOARD_USB1_SS_EN_GPIO_PORT,
                         BOARD_USB1_SS_EN_GPIO_PIN,
                         USB_GPIO_HIGH);
    }

  usb_gpio_set_level(BOARD_USB1_SS_ENABLE_GPIO_PORT,
                     BOARD_USB1_SS_ENABLE_GPIO_PIN,
                     ss_enable);
  return USB_SUCCESS;
}

int
usb_set_power(usb_t usb, usb_power_t power)
{
  switch (usb)
    {
    case USB0:
      switch (power)
        {
        case USB_POWER_0V:
          usb_init();
          usb_gpio_set_level(BOARD_USB0_POWER_5V_ENABLE_GPIO_PORT,
                             BOARD_USB0_POWER_5V_ENABLE_GPIO_PIN,
                             USB_GPIO_LOW);
          return USB_SUCCESS;
        case USB_POWER_5V:
          usb_init();
          usb_gpio_set_level(BOARD_USB0_POWER_5V_ENABLE_GPIO_PORT,
                             BOARD_USB0_POWER_5V_ENABLE_GPIO_PIN,
                             USB_GPIO_HIGH);
          return USB_SUCCESS;
        default:
          return USB_ERROR;
        }

    case USB1:
      switch (power)
        {
        case USB_POWER_0V:
          usb_init();
          usb_gpio_set_level(BOARD_USB1_POWER_5V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_5V_ENABLE_GPIO_PIN,
                             USB_GPIO_LOW);
          usb_gpio_set_level(BOARD_USB1_POWER_20V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_20V_ENABLE_GPIO_PIN,
                             USB_GPIO_LOW);
          return USB_SUCCESS;

        case USB_POWER_5V:
          usb_init();
          usb_gpio_set_level(BOARD_USB1_POWER_5V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_5V_ENABLE_GPIO_PIN,
                             USB_GPIO_HIGH);
          usb_gpio_set_level(BOARD_USB1_POWER_20V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_20V_ENABLE_GPIO_PIN,
                             USB_GPIO_LOW);
          return USB_SUCCESS;

        case USB_POWER_20V:
          usb_init();
          usb_gpio_set_level(BOARD_USB1_POWER_5V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_5V_ENABLE_GPIO_PIN,
                             USB_GPIO_HIGH);
          usb_gpio_set_level(BOARD_USB1_POWER_20V_ENABLE_GPIO_PORT,
                             BOARD_USB1_POWER_20V_ENABLE_GPIO_PIN,
                             USB_GPIO_HIGH);
          return USB_SUCCESS;

        default:
          return USB_ERROR;
        }

    default:
      return USB_ERROR;
    }
}

int
usb_set_vbus_discharge(usb_t usb, bool enable)
{
  switch (usb)
    {
    case USB0:
      usb_init();
      usb_gpio_set_level(BOARD_USB0_VBUS_DISCHARGE_GPIO_PORT,
                         BOARD_USB0_VBUS_DISCHARGE_GPIO_PIN,
                         enable ? USB_GPIO_HIGH : USB_GPIO_LOW);
      return USB_SUCCESS;

    case USB1:
      usb_init();
      usb_gpio_set_level(BOARD_USB1_VBUS_DISCHARGE_GPIO_PORT,
                         BOARD_USB1_VBUS_DISCHARGE_GPIO_PIN,
                         enable ? USB_GPIO_HIGH : USB_GPIO_LOW);
      return USB_SUCCESS;

    default:
      return USB_ERROR;
    }
}

int
usb_set_data(usb_t usb, usb_data_t hs, usb_data_t ss1, usb_data_t ss2)
{
  switch (usb)
    {
    case USB0:
      return usb0_set_data(hs, ss1, ss2);

    case USB1:
      return usb1_set_data(hs, ss1, ss2);

    default:
      return USB_ERROR;
    }
}
