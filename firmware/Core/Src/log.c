#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#include "board.h"
#include "stm32g0xx_hal.h"
#include "tiny_printf.h"

#define LOG_RING_BUFFER_SIZE 4096

typedef struct log_ring_buffer
{
  char buffer[LOG_RING_BUFFER_SIZE];
  char *read;
  char *write;
  volatile bool dma;
  volatile size_t dma_length;
} log_ring_buffer_t;

static UART_HandleTypeDef log_uart;
static DMA_HandleTypeDef log_dma;

static log_ring_buffer_t log_buffer = { .buffer = { 0 },
                                        .read = log_buffer.buffer,
                                        .write = log_buffer.buffer,
                                        .dma = false,
                                        .dma_length = 0 };

static void
log_buffer_put(char c)
{
  char *next = log_buffer.write + 1;
  if (next >= log_buffer.buffer + LOG_RING_BUFFER_SIZE)
    {
      next = log_buffer.buffer;
    }
  if (next == log_buffer.read)
    {
      return;
    }
  *log_buffer.write = c;
  log_buffer.write = next;
}

static size_t
log_buffer_continuous_read_length()
{
  if (log_buffer.write >= log_buffer.read)
    {
      return log_buffer.write - log_buffer.read;
    }
  return log_buffer.buffer + LOG_RING_BUFFER_SIZE - log_buffer.read;
}

static void
log_buffer_mark_dma_end()
{
  log_buffer.read += log_buffer.dma_length;
  if (log_buffer.read >= log_buffer.buffer + LOG_RING_BUFFER_SIZE)
    {
      log_buffer.read = log_buffer.buffer;
    }
  log_buffer.dma = false;
}

static size_t
log_buffer_mark_dma_start()
{
  size_t dma_length = log_buffer_continuous_read_length();
  if (dma_length == 0)
    {
      return 0;
    }
  log_buffer.dma_length = dma_length;
  log_buffer.dma = true;
  return dma_length;
}

static void
log_start_dma()
{
  if (log_buffer.dma)
    {
      return;
    }
  size_t length = log_buffer_mark_dma_start();
  if (length == 0)
    {
      return;
    }

  if (HAL_UART_Transmit_DMA(
          &log_uart, (uint8_t *)log_buffer.read, (uint16_t)length)
      != HAL_OK)
    {
      log_buffer.dma = false;
      log_buffer.dma_length = 0U;
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
  (void)log_uart_init();
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
  log_buffer_mark_dma_end();
  log_start_dma();
  if (!prim)
    {
      __enable_irq();
    }
}

static void
log_tiny_putc(struct tiny_printf_info *info, char ch)
{
  log_buffer_put(ch);
}

void
log_info_v(const char *logger, const char *format, va_list args)
{
  uint32_t prim = __get_PRIMASK();
  __disable_irq();

  while (*logger)
    {
      log_buffer_put(*logger++);
    }
  log_buffer_put(':');
  log_buffer_put(' ');
  struct tiny_printf_info info
      = { .bf = NULL, .zs = 0, .putc = log_tiny_putc };
  tiny_vprintf(&info, format, args);
  log_buffer_put('\r');
  log_buffer_put('\n');
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
