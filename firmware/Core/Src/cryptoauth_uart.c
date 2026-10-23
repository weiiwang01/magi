#include "cryptoauthlib.h"
#include "hal/atca_hal.h"

#include <string.h>

#include "board.h"
#include "timeout.h"

#define CRYPTOAUTH_UART_BAUD 230400U
#define CRYPTOAUTH_UART_TIMEOUT_MS 5U
#define CRYPTOAUTH_UART_RX_SIZE 1024U

static UART_HandleTypeDef cryptoauth_uart;
static DMA_HandleTypeDef cryptoauth_dma;
static volatile uint8_t cryptoauth_rx[CRYPTOAUTH_UART_RX_SIZE];
static size_t cryptoauth_rx_read;

static ATCA_STATUS
cryptoauth_uart_flush()
{
  CLEAR_BIT(cryptoauth_uart.Instance->CR3, USART_CR3_DMAR);
  if (cryptoauth_dma.State == HAL_DMA_STATE_BUSY)
    {
      HAL_DMA_Abort(&cryptoauth_dma);
    }
  __HAL_UART_SEND_REQ(&cryptoauth_uart, UART_RXDATA_FLUSH_REQUEST);
  __HAL_UART_CLEAR_FLAG(&cryptoauth_uart,
                        UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF);
  cryptoauth_rx_read = 0;
  if (HAL_DMA_Start(&cryptoauth_dma,
                    (uint32_t)&cryptoauth_uart.Instance->RDR,
                    (uint32_t)cryptoauth_rx,
                    sizeof(cryptoauth_rx))
      != HAL_OK)
    {
      return ATCA_COMM_FAIL;
    }
  SET_BIT(cryptoauth_uart.Instance->CR3, USART_CR3_DMAR);
  return ATCA_SUCCESS;
}

ATCA_STATUS
hal_uart_init(ATCAIface iface, ATCAIfaceCfg *cfg)
{
  if (iface == NULL || cfg == NULL || cfg->iface_type != ATCA_SWI_IFACE
      || cfg->devtype != ATECC608)
    {
      return ATCA_BAD_PARAM;
    }

  BOARD_CRYPTO_UART_GPIO_CLK_ENABLE();
  BOARD_CRYPTO_UART_CLK_ENABLE();

  GPIO_InitTypeDef gpio_init = { 0 };
  gpio_init.Pin = BOARD_CRYPTO_UART_GPIO_PIN;
  gpio_init.Mode = GPIO_MODE_AF_OD;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = BOARD_CRYPTO_UART_GPIO_AF;
  HAL_GPIO_Init(BOARD_CRYPTO_UART_GPIO_PORT, &gpio_init);

  cryptoauth_uart.Instance = BOARD_CRYPTO_UART_INSTANCE;
  cryptoauth_uart.Init.BaudRate = CRYPTOAUTH_UART_BAUD;
  cryptoauth_uart.Init.WordLength = UART_WORDLENGTH_7B;
  cryptoauth_uart.Init.StopBits = UART_STOPBITS_1;
  cryptoauth_uart.Init.Parity = UART_PARITY_NONE;
  cryptoauth_uart.Init.Mode = UART_MODE_TX_RX;
  cryptoauth_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  cryptoauth_uart.Init.OverSampling = UART_OVERSAMPLING_16;
  cryptoauth_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  cryptoauth_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  if (HAL_HalfDuplex_Init(&cryptoauth_uart) != HAL_OK)
    {
      return ATCA_COMM_FAIL;
    }

  BOARD_CRYPTO_UART_DMA_CLK_ENABLE();
  cryptoauth_dma.Instance = BOARD_CRYPTO_UART_DMA_INSTANCE;
  cryptoauth_dma.Init.Request = BOARD_CRYPTO_UART_DMA_REQUEST;
  cryptoauth_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  cryptoauth_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  cryptoauth_dma.Init.MemInc = DMA_MINC_ENABLE;
  cryptoauth_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  cryptoauth_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  cryptoauth_dma.Init.Mode = DMA_CIRCULAR;
  cryptoauth_dma.Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&cryptoauth_dma) != HAL_OK)
    {
      return ATCA_COMM_FAIL;
    }

  iface->hal_data = &cryptoauth_uart;
  return cryptoauth_uart_flush();
}

ATCA_STATUS
hal_uart_post_init(ATCAIface iface)
{
  return iface != NULL && iface->hal_data == &cryptoauth_uart
                 && cryptoauth_uart.Instance != NULL
             ? ATCA_SUCCESS
             : ATCA_NOT_INITIALIZED;
}

ATCA_STATUS
hal_uart_send(ATCAIface iface,
              uint8_t word_address,
              uint8_t *txdata,
              int txlength)
{
  if (hal_uart_post_init(iface) != ATCA_SUCCESS)
    {
      return ATCA_NOT_INITIALIZED;
    }
  if (txdata == NULL || txlength != 1)
    {
      return ATCA_BAD_PARAM;
    }
  HAL_StatusTypeDef status = HAL_UART_Transmit(
      &cryptoauth_uart, txdata, 1, CRYPTOAUTH_UART_TIMEOUT_MS);
  return status == HAL_OK        ? ATCA_SUCCESS
         : status == HAL_TIMEOUT ? ATCA_TIMEOUT
                                 : ATCA_TX_FAIL;
}

ATCA_STATUS
hal_uart_receive(ATCAIface iface,
                 uint8_t word_address,
                 uint8_t *rxdata,
                 uint16_t *rxlength)
{
  if (hal_uart_post_init(iface) != ATCA_SUCCESS)
    {
      return ATCA_NOT_INITIALIZED;
    }
  if (rxdata == NULL || rxlength == NULL || *rxlength != 1)
    {
      return ATCA_BAD_PARAM;
    }

  *rxlength = 0;
  timeout_t timeout;
  timeout_init(&timeout, CRYPTOAUTH_UART_TIMEOUT_MS);
  while (true)
    {
      if ((cryptoauth_uart.Instance->ISR
           & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE))
              != 0
          || __HAL_DMA_GET_FLAG(&cryptoauth_dma,
                                __HAL_DMA_GET_TE_FLAG_INDEX(&cryptoauth_dma)))
        {
          return ATCA_RX_FAIL;
        }
      size_t write
          = (sizeof(cryptoauth_rx) - __HAL_DMA_GET_COUNTER(&cryptoauth_dma))
            % sizeof(cryptoauth_rx);
      if (cryptoauth_rx_read != write)
        {
          *rxdata = cryptoauth_rx[cryptoauth_rx_read] & 0x7F;
          cryptoauth_rx_read
              = (cryptoauth_rx_read + 1) % sizeof(cryptoauth_rx);
          *rxlength = 1;
          return ATCA_SUCCESS;
        }
      if (timeout_expired(&timeout))
        {
          return ATCA_RX_NO_RESPONSE;
        }
    }
}

ATCA_STATUS
hal_uart_control(ATCAIface iface, uint8_t option, void *param, size_t paramlen)
{
  if (hal_uart_post_init(iface) != ATCA_SUCCESS)
    {
      return ATCA_NOT_INITIALIZED;
    }
  switch (option)
    {
    case ATCA_HAL_FLUSH_BUFFER:
      timeout_sleep_us(100);
      return cryptoauth_uart_flush();
    case ATCA_HAL_CHANGE_BAUD:
      if (param == NULL || paramlen != sizeof(uint32_t))
        {
          return ATCA_BAD_PARAM;
        }
      uint32_t baud;
      memcpy(&baud, param, sizeof(baud));
      if (baud != 115200U && baud != CRYPTOAUTH_UART_BAUD)
        {
          return ATCA_BAD_PARAM;
        }
      cryptoauth_uart.Init.BaudRate = baud;
      CLEAR_BIT(cryptoauth_uart.Instance->CR3, USART_CR3_DMAR);
      if (HAL_HalfDuplex_Init(&cryptoauth_uart) != HAL_OK)
        {
          return ATCA_COMM_FAIL;
        }
      return cryptoauth_uart_flush();
    default:
      return ATCA_UNIMPLEMENTED;
    }
}

ATCA_STATUS
hal_uart_release(void *hal_data)
{
  if (cryptoauth_uart.Instance != NULL)
    {
      CLEAR_BIT(cryptoauth_uart.Instance->CR3, USART_CR3_DMAR);
      if (cryptoauth_dma.Instance != NULL)
        {
          HAL_DMA_Abort(&cryptoauth_dma);
          HAL_DMA_DeInit(&cryptoauth_dma);
        }
      HAL_UART_DeInit(&cryptoauth_uart);
      HAL_GPIO_DeInit(BOARD_CRYPTO_UART_GPIO_PORT, BOARD_CRYPTO_UART_GPIO_PIN);
      BOARD_CRYPTO_UART_CLK_DISABLE();
      cryptoauth_uart.Instance = NULL;
    }
  return ATCA_SUCCESS;
}
