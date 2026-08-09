#include "adc.h"

#include "board.h"

#define ADC_REFERENCE_MV 2048U
#define ADC_MAX_COUNTS 4095U

#define ADC_VBUS_DIVIDER_TOP_OHMS 100000U
#define ADC_VBUS_DIVIDER_BOTTOM_OHMS 5100U

#define ADC_ILM_RESISTOR_OHMS 1000U
#define ADC_ILM_GAIN_UA_PER_A 182U

#define ADC_VBAT_DIVIDER 3U

static ADC_HandleTypeDef adc_handle;
static DMA_HandleTypeDef adc_dma_handle;
static volatile uint16_t adc_results[BOARD_ADC_RESULT_COUNT] = { 0 };

static void
adc_vref_init()
{
  HAL_SYSCFG_VREFBUF_VoltageScalingConfig(SYSCFG_VREFBUF_VOLTAGE_SCALE0);
  HAL_SYSCFG_VREFBUF_HighImpedanceConfig(
      SYSCFG_VREFBUF_HIGH_IMPEDANCE_DISABLE);

  if (HAL_SYSCFG_EnableVREFBUF() != HAL_OK)
    {
      Error_Handler();
    }
}

static void
adc_gpio_init()
{
  GPIO_InitTypeDef gpio_init = { 0 };

  BOARD_ADC_USB0_VBUS_VOLTAGE_GPIO_CLK_ENABLE();
  BOARD_ADC_USB0_INPUT_CURRENT_GPIO_CLK_ENABLE();
  BOARD_ADC_USB1_VBUS_VOLTAGE_GPIO_CLK_ENABLE();
  BOARD_ADC_USB1_INPUT_CURRENT_GPIO_CLK_ENABLE();
  BOARD_ADC_USB1_OUTPUT_CURRENT_GPIO_CLK_ENABLE();

  gpio_init.Mode = GPIO_MODE_ANALOG;
  gpio_init.Pull = GPIO_NOPULL;

  gpio_init.Pin = BOARD_ADC_USB0_VBUS_VOLTAGE_GPIO_PIN;
  HAL_GPIO_Init(BOARD_ADC_USB0_VBUS_VOLTAGE_GPIO_PORT, &gpio_init);

  gpio_init.Pin = BOARD_ADC_USB0_INPUT_CURRENT_GPIO_PIN;
  HAL_GPIO_Init(BOARD_ADC_USB0_INPUT_CURRENT_GPIO_PORT, &gpio_init);

  gpio_init.Pin = BOARD_ADC_USB1_VBUS_VOLTAGE_GPIO_PIN;
  HAL_GPIO_Init(BOARD_ADC_USB1_VBUS_VOLTAGE_GPIO_PORT, &gpio_init);

  gpio_init.Pin = BOARD_ADC_USB1_INPUT_CURRENT_GPIO_PIN;
  HAL_GPIO_Init(BOARD_ADC_USB1_INPUT_CURRENT_GPIO_PORT, &gpio_init);

  gpio_init.Pin = BOARD_ADC_USB1_OUTPUT_CURRENT_GPIO_PIN;
  HAL_GPIO_Init(BOARD_ADC_USB1_OUTPUT_CURRENT_GPIO_PORT, &gpio_init);
}

static void
adc_dma_init()
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  adc_dma_handle.Instance = BOARD_ADC_DMA_INSTANCE;
  adc_dma_handle.Init.Request = BOARD_ADC_DMA_REQUEST;
  adc_dma_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;
  adc_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
  adc_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
  adc_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  adc_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  adc_dma_handle.Init.Mode = DMA_CIRCULAR;
  adc_dma_handle.Init.Priority = DMA_PRIORITY_LOW;

  if (HAL_DMA_Init(&adc_dma_handle) != HAL_OK)
    {
      Error_Handler();
    }

  __HAL_LINKDMA(&adc_handle, DMA_Handle, adc_dma_handle);
}

static void
adc_configure_channel(uint32_t channel)
{
  ADC_ChannelConfTypeDef channel_config = { 0 };

  channel_config.Channel = channel;
  /* Fixed-sequence order comes from the channel number; this only enables it.
   */
  channel_config.Rank = ADC_RANK_CHANNEL_NUMBER;
  channel_config.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;

  if (HAL_ADC_ConfigChannel(&adc_handle, &channel_config) != HAL_OK)
    {
      Error_Handler();
    }
}

static void
adc_peripheral_init()
{
  BOARD_ADC_CLK_ENABLE();

  adc_handle.Instance = BOARD_ADC_INSTANCE;
  adc_handle.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  adc_handle.Init.Resolution = ADC_RESOLUTION_12B;
  adc_handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  adc_handle.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
  adc_handle.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  adc_handle.Init.LowPowerAutoWait = DISABLE;
  adc_handle.Init.LowPowerAutoPowerOff = DISABLE;
  adc_handle.Init.ContinuousConvMode = ENABLE;
  adc_handle.Init.NbrOfConversion = BOARD_ADC_RESULT_COUNT;
  adc_handle.Init.DiscontinuousConvMode = DISABLE;
  adc_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  adc_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  adc_handle.Init.DMAContinuousRequests = ENABLE;
  adc_handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  adc_handle.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
  adc_handle.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;
  adc_handle.Init.OversamplingMode = DISABLE;
  adc_handle.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;

  if (HAL_ADC_Init(&adc_handle) != HAL_OK)
    {
      Error_Handler();
    }

  if (HAL_ADCEx_Calibration_Start(&adc_handle) != HAL_OK)
    {
      Error_Handler();
    }

  adc_configure_channel(BOARD_ADC_USB0_VBUS_VOLTAGE_CHANNEL);
  adc_configure_channel(BOARD_ADC_USB0_INPUT_CURRENT_CHANNEL);
  adc_configure_channel(BOARD_ADC_USB1_VBUS_VOLTAGE_CHANNEL);
  adc_configure_channel(BOARD_ADC_USB1_INPUT_CURRENT_CHANNEL);
  adc_configure_channel(BOARD_ADC_USB1_OUTPUT_CURRENT_CHANNEL);
  adc_configure_channel(BOARD_ADC_TEMPERATURE_CHANNEL);
  adc_configure_channel(BOARD_ADC_VBAT_CHANNEL);
}

static void
adc_start()
{
  if (HAL_DMA_Start(&adc_dma_handle,
                    (uint32_t)&adc_handle.Instance->DR,
                    (uint32_t)adc_results,
                    BOARD_ADC_RESULT_COUNT)
      != HAL_OK)
    {
      Error_Handler();
    }

  SET_BIT(adc_handle.Instance->CFGR1, ADC_CFGR1_DMAEN);

  if (HAL_ADC_Start(&adc_handle) != HAL_OK)
    {
      Error_Handler();
    }
}

static uint32_t
adc_vbus_raw_to_mv(uint16_t raw)
{
  const uint64_t numerator
      = (uint64_t)raw * ADC_REFERENCE_MV
        * (ADC_VBUS_DIVIDER_TOP_OHMS + ADC_VBUS_DIVIDER_BOTTOM_OHMS);
  const uint32_t denominator = ADC_MAX_COUNTS * ADC_VBUS_DIVIDER_BOTTOM_OHMS;

  return (uint32_t)((numerator + (denominator / 2U)) / denominator);
}

static uint32_t
adc_ilm_raw_to_ma(uint16_t raw)
{
  const uint64_t numerator = (uint64_t)raw * ADC_REFERENCE_MV * 1000000U;
  const uint32_t denominator
      = ADC_MAX_COUNTS * ADC_ILM_RESISTOR_OHMS * ADC_ILM_GAIN_UA_PER_A;

  return (uint32_t)((numerator + (denominator / 2U)) / denominator);
}

void
adc_init()
{
  adc_vref_init();
  adc_gpio_init();
  adc_dma_init();
  adc_peripheral_init();
  adc_start();
}

uint32_t
adc_get_usb_voltage(usb_t usb)
{
  switch (usb)
    {
    case USB0:
      return adc_vbus_raw_to_mv(
          adc_results[BOARD_ADC_USB0_VBUS_VOLTAGE_RESULT_INDEX]);
    case USB1:
      return adc_vbus_raw_to_mv(
          adc_results[BOARD_ADC_USB1_VBUS_VOLTAGE_RESULT_INDEX]);
    default:
      return 0U;
    }
}

uint32_t
adc_get_usb_current_in(usb_t usb)
{
  switch (usb)
    {
    case USB0:
      return adc_ilm_raw_to_ma(
          adc_results[BOARD_ADC_USB0_INPUT_CURRENT_RESULT_INDEX]);
    case USB1:
      return adc_ilm_raw_to_ma(
          adc_results[BOARD_ADC_USB1_INPUT_CURRENT_RESULT_INDEX]);
    default:
      return 0U;
    }
}

uint32_t
adc_get_usb_current_out(usb_t usb)
{
  if (usb != USB1)
    {
      return 0U;
    }

  return adc_ilm_raw_to_ma(
      adc_results[BOARD_ADC_USB1_OUTPUT_CURRENT_RESULT_INDEX]);
}

uint32_t
adc_get_battery_voltage()
{
  const uint32_t numerator = (uint32_t)adc_results[BOARD_ADC_VBAT_RESULT_INDEX]
                             * ADC_REFERENCE_MV * ADC_VBAT_DIVIDER;

  return (numerator + (ADC_MAX_COUNTS / 2U)) / ADC_MAX_COUNTS;
}

int32_t
adc_get_temperature()
{
  return __LL_ADC_CALC_TEMPERATURE(
      ADC_REFERENCE_MV,
      adc_results[BOARD_ADC_TEMPERATURE_RESULT_INDEX],
      LL_ADC_RESOLUTION_12B);
}
