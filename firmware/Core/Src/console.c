#include "console.h"

#include "board.h"

#define CONSOLE_INSTANCE(n)                                                   \
  {                                                                           \
    .uart.Instance = BOARD_UNIT##n##_CONSOLE_INSTANCE,                        \
    .dma.Instance = BOARD_UNIT##n##_CONSOLE_DMA_INSTANCE,                     \
    .dma.Init.Request = BOARD_UNIT##n##_CONSOLE_DMA_REQUEST,                  \
    .gpio = BOARD_UNIT##n##_CONSOLE_GPIO_PORT,                                \
    .pins = BOARD_UNIT##n##_CONSOLE_TX_PIN | BOARD_UNIT##n##_CONSOLE_RX_PIN,  \
    .af = BOARD_UNIT##n##_CONSOLE_GPIO_AF,                                    \
    .irq = BOARD_UNIT##n##_CONSOLE_IRQn,                                      \
  }

static console_t consoles[]
    = { CONSOLE_INSTANCE(1), CONSOLE_INSTANCE(2), CONSOLE_INSTANCE(3) };
console_t *console_unit_1 = &consoles[0];
console_t *console_unit_2 = &consoles[1];
console_t *console_unit_3 = &consoles[2];

static void
console_gpio(console_t *console, uint32_t mode)
{
  GPIO_InitTypeDef gpio = {
    .Pin = console->pins,
    .Mode = mode,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
    .Alternate = console->af,
  };
  HAL_GPIO_Init(console->gpio, &gpio);
}

static void
console_notify(console_t *console, bool read, bool write)
{
  if (console->tap)
    {
      stream_notify_send(&console->tap->notify, read, write);
    }
}

static void
console_stop(console_t *console)
{
  CLEAR_BIT(console->uart.Instance->CR1,
            USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE);
  CLEAR_BIT(console->uart.Instance->CR3, USART_CR3_DMAT);
  if (console->dma.State == HAL_DMA_STATE_BUSY)
    {
      HAL_DMA_Abort(&console->dma);
    }
  __HAL_DMA_DISABLE_IT(&console->dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  __HAL_DMA_CLEAR_FLAG(&console->dma,
                       __HAL_DMA_GET_GI_FLAG_INDEX(&console->dma));
  fifo_init(&console->rx_fifo, console->rx_buffer, sizeof console->rx_buffer);
  fifo_init(&console->tx_fifo, console->tx_buffer, sizeof console->tx_buffer);
  console_gpio(console, GPIO_MODE_INPUT);
}

static void
console_start(console_t *console)
{
  if (!console->enabled || !console->tap)
    {
      return;
    }
  console->failed = false;
  console_gpio(console, GPIO_MODE_AF_PP);
  console->uart.Instance->ICR
      = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  console->uart.Instance->RQR = USART_RQR_RXFRQ;
  SET_BIT(console->uart.Instance->CR1,
          USART_CR1_UE | USART_CR1_RXNEIE_RXFNEIE);
}

static void
console_start_tx(console_t *console)
{
  uint8_t *data;
  uint32_t length = fifo_dma_read(&console->tx_fifo, &data, UINT16_MAX);
  if (!length)
    {
      return;
    }
  if (HAL_DMA_Start_IT(&console->dma,
                       (uint32_t)(uintptr_t)data,
                       (uint32_t)(uintptr_t)&console->uart.Instance->TDR,
                       length)
      != HAL_OK)
    {
      fifo_dma_read_done(&console->tx_fifo, 0);
      console->failed = true;
      console_stop(console);
      console_notify(console, true, true);
      return;
    }
  SET_BIT(console->uart.Instance->CR3, USART_CR3_DMAT);
}

static void
console_tx_complete(DMA_HandleTypeDef *dma)
{
  console_t *console = dma->Parent;
  CLEAR_BIT(console->uart.Instance->CR3, USART_CR3_DMAT);
  fifo_dma_read_done(&console->tx_fifo, console->tx_fifo.dma_read);
  console_start_tx(console);
  console_notify(console, false, true);
}

static void
console_tx_error(DMA_HandleTypeDef *dma)
{
  console_t *console = dma->Parent;
  console->failed = true;
  console_stop(console);
  console_notify(console, true, true);
}

int
console_init()
{
  BOARD_CONSOLE_DMA_CLK_ENABLE();
  BOARD_UNIT1_CONSOLE_CLK_ENABLE();
  BOARD_UNIT2_CONSOLE_CLK_ENABLE();
  BOARD_UNIT3_CONSOLE_CLK_ENABLE();
  BOARD_UNIT1_CONSOLE_GPIO_CLK_ENABLE();
  BOARD_UNIT2_CONSOLE_GPIO_CLK_ENABLE();
  BOARD_UNIT3_CONSOLE_GPIO_CLK_ENABLE();
  HAL_NVIC_SetPriority(BOARD_CONSOLE_DMA_IRQn, 3U, 0U);
  for (unsigned int i = 0; i < sizeof consoles / sizeof consoles[0]; ++i)
    {
      console_t *console = &consoles[i];
      if (console->initialized)
        {
          continue;
        }
      console_gpio(console, GPIO_MODE_INPUT);
      console->dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
      console->dma.Init.PeriphInc = DMA_PINC_DISABLE;
      console->dma.Init.MemInc = DMA_MINC_ENABLE;
      console->dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
      console->dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
      console->dma.Init.Mode = DMA_NORMAL;
      console->dma.Init.Priority = DMA_PRIORITY_LOW;
      if (HAL_DMA_Init(&console->dma) != HAL_OK)
        {
          return -1;
        }
      console->dma.Parent = console;
      console->dma.XferCpltCallback = console_tx_complete;
      console->dma.XferErrorCallback = console_tx_error;
      console->uart.Init = (UART_InitTypeDef){
        .BaudRate = 115200U,
        .WordLength = UART_WORDLENGTH_8B,
        .StopBits = UART_STOPBITS_1,
        .Parity = UART_PARITY_NONE,
        .Mode = UART_MODE_TX_RX,
        .HwFlowCtl = UART_HWCONTROL_NONE,
        .OverSampling = UART_OVERSAMPLING_16,
        .OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE,
        .ClockPrescaler = UART_PRESCALER_DIV1,
      };
      if (HAL_UART_Init(&console->uart) != HAL_OK)
        {
          console_stop(console);
          return -1;
        }
      console_stop(console);
      HAL_NVIC_SetPriority(console->irq, 3U, 0U);
      console->initialized = true;
      HAL_NVIC_EnableIRQ(console->irq);
    }
  HAL_NVIC_EnableIRQ(BOARD_CONSOLE_DMA_IRQn);
  return 0;
}

int
console_enable(console_t *console)
{
  if (!console || !console->initialized)
    {
      return -1;
    }
  taskENTER_CRITICAL();
  if (!console->enabled)
    {
      console->enabled = true;
      console_start(console);
      console_notify(console, true, true);
    }
  taskEXIT_CRITICAL();
  return 0;
}

int
console_disable(console_t *console)
{
  if (!console || !console->initialized)
    {
      return -1;
    }
  bool isr = __get_IPSR() != 0U;
  UBaseType_t mask = 0;
  if (isr)
    {
      mask = taskENTER_CRITICAL_FROM_ISR();
    }
  else
    {
      taskENTER_CRITICAL();
    }
  console->enabled = false;
  console_stop(console);
  console_notify(console, true, true);
  if (isr)
    {
      taskEXIT_CRITICAL_FROM_ISR(mask);
    }
  else
    {
      taskEXIT_CRITICAL();
    }
  return 0;
}

bool
console_is_enabled(console_t *console)
{
  taskENTER_CRITICAL();
  bool enabled = console && console->enabled;
  taskEXIT_CRITICAL();
  return enabled;
}

static void
console_detach(console_tap_t *tap)
{
  if (tap->console)
    {
      console_stop(tap->console);
      tap->console->tap = NULL;
      tap->console = NULL;
      stream_notify_send(&tap->notify, true, true);
    }
}

int
console_tap_attach(console_tap_t *tap, console_t *console)
{
  if (!tap || !console || !console->initialized)
    {
      return -1;
    }
  taskENTER_CRITICAL();
  if (tap->console != console)
    {
      console_detach(tap);
      if (console->tap)
        {
          console_detach(console->tap);
        }
      tap->console = console;
      console->tap = tap;
      console_start(console);
      console_notify(console, true, true);
    }
  taskEXIT_CRITICAL();
  return 0;
}

int
console_tap_detach(console_tap_t *tap)
{
  if (!tap)
    {
      return -1;
    }
  taskENTER_CRITICAL();
  console_detach(tap);
  taskEXIT_CRITICAL();
  return 0;
}

static void
console_stream_notify(stream_t *stream, uint32_t read, uint32_t write)
{
  console_tap_t *tap = container_of(stream, console_tap_t, stream);
  taskENTER_CRITICAL();
  stream_notify_set(&tap->notify, read, write);
  console_t *console = tap->console;
  bool terminal = !console || !console->enabled || console->failed;
  stream_notify_send(&tap->notify,
                     terminal || fifo_size(&console->rx_fifo) != 0,
                     terminal
                         || fifo_size(&console->tx_fifo)
                                < fifo_capacity(&console->tx_fifo));
  taskEXIT_CRITICAL();
}

static stream_status_t
console_stream_exchange(stream_t *stream,
                        uint8_t *read,
                        uint8_t *write,
                        stream_opts_t *opts)
{
  console_tap_t *tap = container_of(stream, console_tap_t, stream);
  for (;;)
    {
      taskENTER_CRITICAL();
      console_t *console = tap->console;
      stream_status_t status = 0;
      if (!console || console->failed)
        {
          status = STREAM_ABORT;
        }
      else if (!console->enabled)
        {
          status = STREAM_CLOSED;
        }
      else
        {
          if (read && fifo_getc(&console->rx_fifo, read))
            {
              status |= STREAM_READ_OK;
            }
          if (write && fifo_putc(&console->tx_fifo, *write))
            {
              status |= STREAM_WRITE_OK;
              console_start_tx(console);
              if (console->failed)
                {
                  status |= STREAM_ABORT;
                }
            }
        }
      if (status || (!read && !write) || (opts && opts->nonblocking))
        {
          taskEXIT_CRITICAL();
          return status;
        }
      tap->notify.waiter = xTaskGetCurrentTaskHandle();
      taskEXIT_CRITICAL();
      xTaskNotifyWait(0, UINT32_MAX, NULL, portMAX_DELAY);
      taskENTER_CRITICAL();
      tap->notify.waiter = NULL;
      taskEXIT_CRITICAL();
    }
}

int
console_tap_init(console_tap_t *tap)
{
  if (!tap)
    {
      return -1;
    }
  *tap = (console_tap_t){ .stream = { .exchange = console_stream_exchange,
                                      .notify = console_stream_notify } };
  return 0;
}

void
console_uart_irq(console_t *console)
{
  if (!console->initialized || !console->enabled || !console->tap
      || console->failed)
    {
      return;
    }
  USART_TypeDef *uart = console->uart.Instance;
  uint32_t status = uart->ISR;
  if (status & USART_ISR_RXNE_RXFNE)
    {
      uint8_t byte = (uint8_t)uart->RDR;
      if (!(status & (USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
          && fifo_putc(&console->rx_fifo, byte))
        {
          console_notify(console, true, false);
        }
    }
  uart->ICR
      = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
}

void
console_dma_irq()
{
  for (unsigned int i = 0; i < sizeof consoles / sizeof consoles[0]; ++i)
    {
      if (consoles[i].initialized && consoles[i].enabled && consoles[i].tap
          && !consoles[i].failed)
        {
          HAL_DMA_IRQHandler(&consoles[i].dma);
        }
    }
}
