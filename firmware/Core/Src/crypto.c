#include "crypto.h"

#include <stdbool.h>

#include "cryptoauthlib.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "main.h"
#include "board.h"
#if BOARD_VERSION == 0x0200
#include "xpander.h"
#endif
#include <wolfssl/wolfcrypt/port/atmel/atmel.h>

static SemaphoreHandle_t crypto_mutex;
static bool crypto_ready;
static ATCAIfaceCfg crypto_config = {
#if BOARD_VERSION == 0x0200
  .iface_type = ATCA_I2C_IFACE,
  .atcai2c = { .address = 0xC0, .bus = 0, .baud = 100000 },
#else
  .iface_type = ATCA_SWI_IFACE,
  .atcaswi = { .bus = 0 },
#endif
  .devtype = ATECC608,
  .wake_delay = 1500,
  .rx_retries = 3,
};

static int
crypto_allocate_slot(int type)
{
  return type == ATMEL_SLOT_ECDHE ? ATCA_TEMPKEY_KEYID : ATECC_INVALID_SLOT;
}

static void
crypto_free_slot(int slot)
{
}

void
crypto_init()
{
#if BOARD_VERSION == 0x0200
  xpander_init();
#endif
  crypto_mutex = xSemaphoreCreateRecursiveMutex();
  if (crypto_mutex == NULL)
    {
      Error_Handler();
    }
}

int
crypto_lock()
{
  if (xSemaphoreTakeRecursive(crypto_mutex, portMAX_DELAY) != pdTRUE)
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
      wolfCrypt_ATECC_SetConfig(&crypto_config);
      if (atmel_init() != 0)
        {
          crypto_unlock();
          return ATCA_COMM_FAIL;
        }
      atmel_set_slot_allocator(crypto_allocate_slot, crypto_free_slot);
      crypto_ready = true;
    }
  return ATCA_SUCCESS;
}

void
crypto_unlock()
{
  if (xSemaphoreGiveRecursive(crypto_mutex) != pdTRUE)
    {
      Error_Handler();
    }
}
