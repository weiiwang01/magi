#include "ws2812.h"

#include "board.h"
#include "main.h"

#define WS2812_BIT_RATE_HZ 800000U
#define WS2812_RESET_SLOTS 220U
#define WS2812_BITS_PER_LED 24U
#define WS2812_LED_DATA_LENGTH (WS2812_MAX_LEDS * WS2812_BITS_PER_LED)
#define WS2812_BUFFER_LENGTH (WS2812_LED_DATA_LENGTH + WS2812_RESET_SLOTS)
#define WS2812_T0H_NS 350U
#define WS2812_T1H_NS 700U

static TIM_HandleTypeDef ws2812_timer_handle;
static DMA_HandleTypeDef ws2812_dma_handle;
static uint32_t ws2812_buffer[WS2812_BUFFER_LENGTH] = { 0 };
static uint32_t ws2812_low = 0;
static uint32_t ws2812_high = 0;
static uint8_t ws2812_started = 0;

static uint32_t
ws2812_get_timer_clock_hz()
{
  RCC_ClkInitTypeDef clock_config = { 0 };
  uint32_t flash_latency;
  uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();

  HAL_RCC_GetClockConfig(&clock_config, &flash_latency);
  if (clock_config.APB1CLKDivider != RCC_HCLK_DIV1)
    {
      timer_clock *= 2U;
    }

  return timer_clock;
}

static uint32_t
ws2812_ticks_from_ns(uint32_t timer_clock_hz, uint32_t nanoseconds)
{
  uint64_t ticks = ((uint64_t)timer_clock_hz * nanoseconds + 500000000ULL)
                   / 1000000000ULL;

  if (ticks == 0U)
    {
      ticks = 1U;
    }

  return (uint32_t)ticks;
}

static void
ws2812_gpio_init()
{
  GPIO_InitTypeDef gpio_init = { 0 };

  BOARD_WS2812_GPIO_CLK_ENABLE();

  gpio_init.Pin = BOARD_WS2812_GPIO_PIN;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = BOARD_WS2812_GPIO_AF;
  HAL_GPIO_Init(BOARD_WS2812_GPIO_PORT, &gpio_init);
}

static void
ws2812_dma_init()
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  ws2812_dma_handle.Instance = BOARD_WS2812_DMA_INSTANCE;
  ws2812_dma_handle.Init.Request = BOARD_WS2812_DMA_REQUEST;
  ws2812_dma_handle.Init.Direction = DMA_MEMORY_TO_PERIPH;
  ws2812_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
  ws2812_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
  ws2812_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  ws2812_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  ws2812_dma_handle.Init.Mode = DMA_CIRCULAR;
  ws2812_dma_handle.Init.Priority = DMA_PRIORITY_LOW;

  if (HAL_DMA_Init(&ws2812_dma_handle) != HAL_OK)
    {
      Error_Handler();
    }

  __HAL_LINKDMA(
      &ws2812_timer_handle, hdma[BOARD_WS2812_TIM_DMA_ID], ws2812_dma_handle);
}

static void
ws2812_timer_init(uint32_t period_ticks)
{
  TIM_OC_InitTypeDef output_compare_init = { 0 };

  BOARD_WS2812_TIM_CLK_ENABLE();

  ws2812_timer_handle.Instance = BOARD_WS2812_TIM_INSTANCE;
  ws2812_timer_handle.Init.Prescaler = 0;
  ws2812_timer_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
  ws2812_timer_handle.Init.Period = period_ticks - 1U;
  ws2812_timer_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  ws2812_timer_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

  if (HAL_TIM_PWM_Init(&ws2812_timer_handle) != HAL_OK)
    {
      Error_Handler();
    }

  output_compare_init.OCMode = TIM_OCMODE_PWM1;
  output_compare_init.Pulse = 0;
  output_compare_init.OCPolarity = TIM_OCPOLARITY_HIGH;
  output_compare_init.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(
          &ws2812_timer_handle, &output_compare_init, BOARD_WS2812_TIM_CHANNEL)
      != HAL_OK)
    {
      Error_Handler();
    }
}

void
ws2812_init()
{
  const uint32_t timer_clock = ws2812_get_timer_clock_hz();
  const uint32_t period_ticks = timer_clock / WS2812_BIT_RATE_HZ;

  if ((period_ticks == 0U) || (period_ticks > UINT16_MAX))
    {
      Error_Handler();
    }

  ws2812_low = ws2812_ticks_from_ns(timer_clock, WS2812_T0H_NS);
  ws2812_high = ws2812_ticks_from_ns(timer_clock, WS2812_T1H_NS);
  if ((ws2812_low >= period_ticks) || (ws2812_high >= period_ticks))
    {
      Error_Handler();
    }

  for (size_t index = 0; index < WS2812_LED_DATA_LENGTH; index++)
    {
      ws2812_buffer[index] = ws2812_low;
    }
  for (size_t index = WS2812_LED_DATA_LENGTH; index < WS2812_BUFFER_LENGTH;
       index++)
    {
      ws2812_buffer[index] = 0;
    }

  ws2812_gpio_init();
  ws2812_timer_init(period_ticks);
  ws2812_dma_init();

  if (ws2812_started == 0U)
    {
      if (HAL_TIM_PWM_Start_DMA(&ws2812_timer_handle,
                                BOARD_WS2812_TIM_CHANNEL,
                                ws2812_buffer,
                                WS2812_BUFFER_LENGTH)
          != HAL_OK)
        {
          Error_Handler();
        }

      __HAL_DMA_DISABLE_IT(&ws2812_dma_handle,
                           DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
      ws2812_started = 1U;
    }
}

void
ws2812_set_rgb(size_t index, uint8_t red, uint8_t green, uint8_t blue)
{
  if (index >= WS2812_MAX_LEDS)
    {
      return;
    }

  for (uint32_t bit = 0; bit < 8U; bit++)
    {
      const uint8_t mask = (uint8_t)(1U << (7U - bit));

      ws2812_buffer[index * WS2812_BITS_PER_LED + bit]
          = ((green & mask) != 0U) ? ws2812_high : ws2812_low;
      ws2812_buffer[index * WS2812_BITS_PER_LED + bit + 8U]
          = ((red & mask) != 0U) ? ws2812_high : ws2812_low;
      ws2812_buffer[index * WS2812_BITS_PER_LED + bit + 16U]
          = ((blue & mask) != 0U) ? ws2812_high : ws2812_low;
    }
}

void
ws2812_set_hsv(size_t index, uint8_t hue, uint8_t saturation, uint8_t value)
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  if (saturation == 0U)
    {
      ws2812_set_rgb(index, value, value, value);
      return;
    }

  const uint8_t region = hue / 43U;
  const uint8_t remainder = (uint8_t)((hue - (region * 43U)) * 6U);
  const uint8_t p = (uint8_t)(((uint16_t)value * (255U - saturation)) >> 8U);
  const uint8_t q
      = (uint8_t)(((uint16_t)value
                   * (255U - (((uint16_t)saturation * remainder) >> 8U)))
                  >> 8U);
  const uint8_t t
      = (uint8_t)(((uint16_t)value
                   * (255U
                      - (((uint16_t)saturation * (255U - remainder)) >> 8U)))
                  >> 8U);

  switch (region)
    {
    case 0:
      red = value;
      green = t;
      blue = p;
      break;
    case 1:
      red = q;
      green = value;
      blue = p;
      break;
    case 2:
      red = p;
      green = value;
      blue = t;
      break;
    case 3:
      red = p;
      green = q;
      blue = value;
      break;
    case 4:
      red = t;
      green = p;
      blue = value;
      break;
    default:
      red = value;
      green = p;
      blue = q;
      break;
    }

  ws2812_set_rgb(index, red, green, blue);
}
