#ifndef UNIT_H
#define UNIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32g0xx_hal.h"
#include "timeout.h"

#define UNIT_COUNT 3U

typedef enum
{
  UNIT_TYPE_CM5 = 0,
  UNIT_TYPE_CM4 = 1
} unit_type_t;

static const char *const unit_type_names[] = {
  [UNIT_TYPE_CM5] = "cm5",
  [UNIT_TYPE_CM4] = "cm4",
};

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} unit_gpio_t;

typedef struct unit unit_t;
struct unit
{
  unit_type_t type;
  unit_gpio_t power;
  unit_gpio_t pcie_power;
  unit_gpio_t power_button;
  unit_gpio_t rpiboot;
  unit_gpio_t activity_led;
  unit_gpio_t power_led;
  unit_gpio_t sense_3v3;
  struct console *console;
  size_t ws2812_index;
  timeout_t power_button_press;
  bool power_button_pressed;
};

extern unit_t *unit_1;
extern unit_t *unit_2;
extern unit_t *unit_3;

void unit_init();
void unit_exti_irq();

int unit_set_type(unit_t *unit, unit_type_t type);
unit_type_t unit_get_type(const unit_t *unit);
void unit_set_power(unit_t *unit, bool enable);
bool unit_get_power(const unit_t *unit);
void unit_set_pcie_power(unit_t *unit, bool enable);
void unit_set_power_button(unit_t *unit, bool enable);
void unit_set_power_button_press(unit_t *unit, uint32_t ms);
uint32_t unit_get_power_button_press(const unit_t *unit);
void unit_set_rpiboot(unit_t *unit, bool enable);
bool unit_get_rpiboot(const unit_t *unit);
bool unit_get_3v3(const unit_t *unit);
bool unit_get_power_led(const unit_t *unit);
bool unit_get_activity_led(const unit_t *unit);

#endif /* UNIT_H */
