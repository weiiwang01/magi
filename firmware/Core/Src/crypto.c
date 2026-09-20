#include "crypto.h"

#include <stdbool.h>

#include "cryptoauthlib.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "main.h"
#include "xpander.h"

static SemaphoreHandle_t crypto_mutex;
static bool crypto_ready;
static ATCAIfaceCfg crypto_config = {
  .iface_type = ATCA_I2C_IFACE,
  .devtype = ATECC608,
  .atcai2c = { .address = 0xC0, .bus = 0, .baud = 100000 },
  .wake_delay = 1500,
  .rx_retries = 3,
};

void
crypto_init()
{
  xpander_init();
  crypto_mutex = xSemaphoreCreateMutex();
  if (crypto_mutex == NULL)
    {
      Error_Handler();
    }
}

int
crypto_lock()
{
  if (xSemaphoreTake(crypto_mutex, portMAX_DELAY) != pdTRUE)
    {
      Error_Handler();
    }
  if (!crypto_ready)
    {
      ATCA_STATUS status = atcab_init(&crypto_config);
      if (status != ATCA_SUCCESS)
        {
          crypto_unlock();
          return status;
        }
      crypto_ready = true;
    }
  return ATCA_SUCCESS;
}

void
crypto_unlock()
{
  if (xSemaphoreGive(crypto_mutex) != pdTRUE)
    {
      Error_Handler();
    }
}
