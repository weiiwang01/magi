#include "xpander.h"

#include <errno.h>
#include <stddef.h>

#include "board.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "timeout.h"

typedef enum
{
  XPANDER_VERSION = 0xFF,
  XPANDER_STATUS = 0xF0,
  XPANDER_BUFFER_LENGTH = 0xE1,
  XPANDER_BUFFER_READ = 0xE2,
  XPANDER_I2C_STATUS = 0x20,
  XPANDER_I2C_WRITE = 0x21,
  XPANDER_I2C_READ = 0x22,
} xpander_command_t;

#define XPANDER_TIMEOUT_MS 100U
#define XPANDER_SPI_GAP_US 5U

static SPI_HandleTypeDef xpander_spi;
static SemaphoreHandle_t xpander_mutex;

static int
xpander_transaction(const uint8_t *request,
                    unsigned int request_length,
                    uint8_t *response,
                    unsigned int response_length)
{
  int error = 0;
  HAL_GPIO_WritePin(
      BOARD_XPANDER_CS_GPIO_PORT, BOARD_XPANDER_CS_GPIO_PIN, GPIO_PIN_RESET);
  timeout_sleep_us(XPANDER_SPI_GAP_US);
  for (unsigned int i = 0; i < request_length + response_length; ++i)
    {
      uint8_t tx = i < request_length ? request[i] : 0;
      uint8_t rx = 0;
      if (HAL_SPI_TransmitReceive(
              &xpander_spi, &tx, &rx, 1, XPANDER_TIMEOUT_MS)
          != HAL_OK)
        {
          error = EIO;
          break;
        }
      if (i >= request_length)
        {
          response[i - request_length] = rx;
        }
    }
  HAL_GPIO_WritePin(
      BOARD_XPANDER_CS_GPIO_PORT, BOARD_XPANDER_CS_GPIO_PIN, GPIO_PIN_SET);
  timeout_sleep_us(XPANDER_SPI_GAP_US);
  return error;
}

static int
xpander_query(xpander_command_t command,
              uint8_t *response,
              unsigned int length)
{
  uint8_t request = (uint8_t)command;
  return xpander_transaction(&request, 1, response, length);
}

static int
xpander_wait_idle()
{
  timeout_t timeout;
  timeout_init(&timeout, XPANDER_TIMEOUT_MS);
  do
    {
      uint8_t status;
      int error = xpander_query(XPANDER_STATUS, &status, 1);
      if (error != 0)
        {
          return error;
        }
      if (status == 0)
        {
          return 0;
        }
      if (status != 1)
        {
          return EPROTO;
        }
      timeout_sleep_ms(1);
    }
  while (!timeout_expired(&timeout));
  return ETIMEDOUT;
}

static int
xpander_i2c_result()
{
  int error = xpander_wait_idle();
  uint8_t status;
  if (error == 0)
    {
      error = xpander_query(XPANDER_I2C_STATUS, &status, 1);
      if (error == 0 && status != 0)
        {
          error = ENXIO;
        }
    }
  return error;
}

static int
xpander_lock()
{
  if (xpander_mutex == NULL)
    {
      return ENODEV;
    }
  return xSemaphoreTake(xpander_mutex, portMAX_DELAY) == pdTRUE ? 0 : EIO;
}

static void
xpander_unlock()
{
  if (xSemaphoreGive(xpander_mutex) != pdTRUE)
    {
      Error_Handler();
    }
}

void
xpander_init()
{
  GPIO_InitTypeDef gpio = { 0 };
  BOARD_XPANDER_SPI_CLK_ENABLE();
  BOARD_XPANDER_SPI_SCK_GPIO_CLK_ENABLE();
  BOARD_XPANDER_SPI_MISO_GPIO_CLK_ENABLE();
  BOARD_XPANDER_SPI_MOSI_GPIO_CLK_ENABLE();
  BOARD_XPANDER_CS_GPIO_CLK_ENABLE();

  HAL_GPIO_WritePin(
      BOARD_XPANDER_CS_GPIO_PORT, BOARD_XPANDER_CS_GPIO_PIN, GPIO_PIN_SET);
  gpio.Pin = BOARD_XPANDER_CS_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BOARD_XPANDER_CS_GPIO_PORT, &gpio);

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pin = BOARD_XPANDER_SPI_SCK_GPIO_PIN;
  gpio.Alternate = BOARD_XPANDER_SPI_SCK_GPIO_AF;
  HAL_GPIO_Init(BOARD_XPANDER_SPI_SCK_GPIO_PORT, &gpio);
  gpio.Pin = BOARD_XPANDER_SPI_MISO_GPIO_PIN;
  gpio.Alternate = BOARD_XPANDER_SPI_MISO_GPIO_AF;
  HAL_GPIO_Init(BOARD_XPANDER_SPI_MISO_GPIO_PORT, &gpio);
  gpio.Pin = BOARD_XPANDER_SPI_MOSI_GPIO_PIN;
  gpio.Alternate = BOARD_XPANDER_SPI_MOSI_GPIO_AF;
  HAL_GPIO_Init(BOARD_XPANDER_SPI_MOSI_GPIO_PORT, &gpio);

  xpander_spi.Instance = BOARD_XPANDER_SPI_INSTANCE;
  xpander_spi.Init.Mode = SPI_MODE_MASTER;
  xpander_spi.Init.Direction = SPI_DIRECTION_2LINES;
  xpander_spi.Init.DataSize = SPI_DATASIZE_8BIT;
  xpander_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  xpander_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  xpander_spi.Init.NSS = SPI_NSS_SOFT;
  xpander_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  xpander_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  xpander_spi.Init.TIMode = SPI_TIMODE_DISABLE;
  xpander_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  xpander_spi.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  xpander_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&xpander_spi) != HAL_OK)
    {
      Error_Handler();
    }
  xpander_mutex = xSemaphoreCreateMutex();
  if (xpander_mutex == NULL)
    {
      Error_Handler();
    }
}

int
xpander_get_version(uint8_t *version)
{
  if (version == NULL)
    {
      return EINVAL;
    }
  int error = xpander_lock();
  if (error == 0)
    {
      error = xpander_query(XPANDER_VERSION, version, 1);
      xpander_unlock();
    }
  return error;
}

int
xpander_i2c_write(uint8_t address, const uint8_t *data, unsigned int length)
{
  if ((address & 1U) != 0 || data == NULL || length == 0 || length > UINT8_MAX)
    {
      return EINVAL;
    }
  int error = xpander_lock();
  if (error != 0)
    {
      return error;
    }
  uint8_t request[3 + UINT8_MAX]
      = { XPANDER_I2C_WRITE, address, (uint8_t)length };
  for (unsigned int i = 0; i < length; ++i)
    {
      request[3 + i] = data[i];
    }
  error = xpander_wait_idle();
  if (error == 0)
    {
      error = xpander_transaction(request, 3 + length, NULL, 0);
    }
  if (error == 0)
    {
      error = xpander_i2c_result();
    }
  xpander_unlock();
  return error;
}

int
xpander_i2c_read(uint8_t address, uint8_t *data, unsigned int length)
{
  if ((address & 1U) == 0 || data == NULL || length == 0 || length > UINT8_MAX)
    {
      return EINVAL;
    }
  int error = xpander_lock();
  if (error != 0)
    {
      return error;
    }
  uint8_t request[] = { XPANDER_I2C_READ, address, (uint8_t)length };
  error = xpander_wait_idle();
  if (error == 0)
    {
      error = xpander_transaction(request, sizeof(request), NULL, 0);
    }
  if (error == 0)
    {
      error = xpander_i2c_result();
    }
  if (error == 0)
    {
      uint8_t bytes[4];
      error = xpander_query(XPANDER_BUFFER_LENGTH, bytes, sizeof(bytes));
      if (error == 0)
        {
          uint32_t available = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
                               | ((uint32_t)bytes[2] << 16)
                               | ((uint32_t)bytes[3] << 24);
          if (available != length)
            {
              error = EPROTO;
            }
        }
    }
  if (error == 0)
    {
      error = xpander_query(XPANDER_BUFFER_READ, data, length);
    }
  xpander_unlock();
  return error;
}
