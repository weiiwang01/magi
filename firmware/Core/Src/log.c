#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "fifo.h"
#include "stm32g0xx_hal.h"
#include "tiny_printf.h"

#define LOG_BUFFER_SIZE 1024

static UART_HandleTypeDef log_uart;
static DMA_HandleTypeDef log_dma;

static uint8_t log_storage[LOG_BUFFER_SIZE];
static fifo_t log_buffer;

static void
log_start_dma()
{
  uint8_t *data;
  uint32_t length = fifo_dma_read(&log_buffer, &data, UINT16_MAX);
  if (length == 0)
    {
      return;
    }

  if (HAL_UART_Transmit_DMA(&log_uart, data, (uint16_t)length) != HAL_OK)
    {
      fifo_dma_read_done(&log_buffer, 0);
    }
}

static HAL_StatusTypeDef
log_uart_init()
{
  GPIO_InitTypeDef gpio_init = { 0 };

  BOARD_LOG_UART_CLK_ENABLE();
  BOARD_LOG_UART_GPIO_CLK_ENABLE();
  BOARD_LOG_UART_DMA_CLK_ENABLE();

  gpio_init.Pin = BOARD_LOG_UART_GPIO_PIN;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = BOARD_LOG_UART_GPIO_AF;
  HAL_GPIO_Init(BOARD_LOG_UART_GPIO_PORT, &gpio_init);

  log_dma.Instance = BOARD_LOG_UART_DMA_INSTANCE;
  log_dma.Init.Request = BOARD_LOG_UART_DMA_REQUEST;
  log_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
  log_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  log_dma.Init.MemInc = DMA_MINC_ENABLE;
  log_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  log_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  log_dma.Init.Mode = DMA_NORMAL;
  log_dma.Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&log_dma) != HAL_OK)
    {
      return HAL_ERROR;
    }

  log_uart.Instance = BOARD_LOG_UART_INSTANCE;
  log_uart.Init.BaudRate = 115200U;
  log_uart.Init.WordLength = UART_WORDLENGTH_8B;
  log_uart.Init.StopBits = UART_STOPBITS_1;
  log_uart.Init.Parity = UART_PARITY_NONE;
  log_uart.Init.Mode = UART_MODE_TX;
  log_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  log_uart.Init.OverSampling = UART_OVERSAMPLING_16;
  log_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  log_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  log_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  __HAL_LINKDMA(&log_uart, hdmatx, log_dma);

  if (HAL_UART_Init(&log_uart) != HAL_OK)
    {
      return HAL_ERROR;
    }
  if (HAL_UARTEx_SetTxFifoThreshold(&log_uart, UART_TXFIFO_THRESHOLD_1_8)
      != HAL_OK)
    {
      return HAL_ERROR;
    }
  if (HAL_UARTEx_DisableFifoMode(&log_uart) != HAL_OK)
    {
      return HAL_ERROR;
    }

  HAL_NVIC_SetPriority(BOARD_LOG_UART_DMA_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(BOARD_LOG_UART_DMA_IRQn);
  HAL_NVIC_SetPriority(BOARD_LOG_UART_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(BOARD_LOG_UART_IRQn);
  return HAL_OK;
}

void
log_init()
{
  fifo_init(&log_buffer, log_storage, sizeof log_storage);
  log_uart_init();
}

void
log_dma_irq()
{
  HAL_DMA_IRQHandler(&log_dma);
}

void
log_uart_irq()
{
  HAL_UART_IRQHandler(&log_uart);
}

void
HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart != &log_uart)
    {
      return;
    }

  uint32_t prim = __get_PRIMASK();
  __disable_irq();
  fifo_dma_read_done(&log_buffer, uart->TxXferSize);
  log_start_dma();
  if (!prim)
    {
      __enable_irq();
    }
}

static void
log_tiny_putc(struct tiny_printf_info *info, char ch)
{
  fifo_putc(&log_buffer, (uint8_t)ch);
}

void
log_info_v(const char *logger, const char *format, va_list args)
{
  uint32_t prim = __get_PRIMASK();
  __disable_irq();

  while (*logger)
    {
      fifo_putc(&log_buffer, (uint8_t)*logger++);
    }
  fifo_putc(&log_buffer, ':');
  fifo_putc(&log_buffer, ' ');
  struct tiny_printf_info info
      = { .bf = NULL, .zs = 0, .putc = log_tiny_putc };
  tiny_vprintf(&info, format, args);
  fifo_putc(&log_buffer, '\r');
  fifo_putc(&log_buffer, '\n');
  log_start_dma();

  if (!prim)
    {
      __enable_irq();
    }
}

void
log_info(const char *logger, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  log_info_v(logger, format, args);
  va_end(args);
}
