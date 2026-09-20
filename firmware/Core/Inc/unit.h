#ifndef UNIT_H
#define UNIT_H

#include <stdbool.h>
#include <stddef.h>
#include "stm32g0xx_hal.h"

#define UNIT_COUNT 3U

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} unit_gpio_t;

typedef struct unit unit_t;
struct unit
{
  unit_gpio_t power;
  unit_gpio_t pcie_power;
  unit_gpio_t power_button;
  unit_gpio_t activity_led;
  unit_gpio_t power_led;
  unit_gpio_t sense_3v3;
  struct console *console;
  size_t ws2812_index;
};

extern unit_t *unit_1;
extern unit_t *unit_2;
extern unit_t *unit_3;

void unit_init();
void unit_exti_irq();

void unit_set_power(unit_t *unit, bool enable);
void unit_set_pcie_power(unit_t *unit, bool enable);
/* The unit task also mirrors the MCU button onto this output every poll. */
void unit_set_power_button(unit_t *unit, bool enable);
bool unit_get_3v3(const unit_t *unit);
bool unit_get_power_led(const unit_t *unit);
bool unit_get_activity_led(const unit_t *unit);

#endif /* UNIT_H */
