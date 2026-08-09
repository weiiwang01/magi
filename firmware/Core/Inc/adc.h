#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#include "usb.h"

void adc_init();

uint32_t adc_get_usb_voltage(usb_t usb);
uint32_t adc_get_usb_current_in(usb_t usb);
uint32_t adc_get_usb_current_out(usb_t usb);
uint32_t adc_get_battery_voltage();
int32_t adc_get_temperature();

#endif /* ADC_H */
