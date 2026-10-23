#include "cryptoauthlib.h"
#include "hal/atca_hal.h"

#include "timeout.h"

void
hal_delay_us(uint32_t delay)
{
  timeout_sleep_us(delay);
}

void
hal_delay_ms(uint32_t delay)
{
  timeout_sleep_ms(delay + (delay != 0 ? portTICK_PERIOD_MS : 0));
}
