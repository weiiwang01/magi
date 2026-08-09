#ifndef USB_H
#define USB_H

#include <stdbool.h>

#define USB_SUCCESS 0
#define USB_ERROR (-1)

typedef enum
{
  USB0 = 0,
  USB1,
} usb_t;

typedef enum
{
  USB_POWER_0V,
  USB_POWER_5V,
  USB_POWER_20V,
} usb_power_t;

typedef enum
{
  USB_DATA_NONE,
  USB_DATA_USB,
  USB_DATA_ALT_MODE,
} usb_data_t;

int usb_set_power(usb_t usb, usb_power_t power);
int usb_set_vbus_discharge(usb_t usb, bool enable);
int usb_set_data(usb_t usb, usb_data_t hs, usb_data_t ss1, usb_data_t ss2);

#endif /* USB_H */
