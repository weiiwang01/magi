#ifndef UNIT_H
#define UNIT_H

#include <stdbool.h>

#define UNIT_SUCCESS 0
#define UNIT_ERROR (-1)

typedef enum
{
  UNIT1 = 0,
  UNIT2,
  UNIT3,
  UNIT_COUNT,
} unit_t;

void unit_init();

int unit_set_power(unit_t unit, bool enable);
int unit_set_pcie_power(unit_t unit, bool enable);
int unit_set_power_button(unit_t unit, bool enable);
int unit_get_3v3(unit_t unit, bool *on);
int unit_get_power_led(unit_t unit, bool *on);
int unit_get_activity_led(unit_t unit, bool *on);

#endif /* UNIT_H */
