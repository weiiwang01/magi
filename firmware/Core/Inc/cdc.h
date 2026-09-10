#ifndef CDC_H
#define CDC_H

#include "usbd_def.h"

void cdc_init();

uint8_t cdc_usb_init(USBD_HandleTypeDef *device);
void cdc_usb_deinit();
void cdc_usb_receive(uint8_t *data, uint32_t length);
void cdc_usb_transmit_complete(uint32_t length);

#endif /* CDC_H */
