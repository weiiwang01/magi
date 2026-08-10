#include "unit.h"

#include "board.h"
#include "cmsis_os.h"
#include "ws2812.h"

#define UNIT_LED_POLL_INTERVAL_MS 50U
#define UNIT_LED_ON 16U

static bool unit_initialized;
static osThreadId_t unit_task_handle;
static const osThreadAttr_t unit_task_attributes = {
  .name = "units",
  .priority = (osPriority_t)osPriorityLow,
  .stack_size = 128U * 4U,
};

static void unit_task(void *argument);

static void
unit_gpio_output_init(GPIO_TypeDef *port,
                      uint16_t pin,
                      uint32_t mode,
                      uint32_t pull,
                      GPIO_PinState state)
{
  GPIO_InitTypeDef gpio_init = { 0 };

  HAL_GPIO_WritePin(port, pin, state);

  gpio_init.Pin = pin;
  gpio_init.Mode = mode;
  gpio_init.Pull = pull;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gpio_init);
}

static void
unit_gpio_input_init(GPIO_TypeDef *port, uint16_t pin, uint32_t pull)
{
  GPIO_InitTypeDef gpio_init = { 0 };

  gpio_init.Pin = pin;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = pull;
  HAL_GPIO_Init(port, &gpio_init);
}

static void
unit_gpio_init()
{
  if (unit_initialized)
    {
      return;
    }

  BOARD_UNIT1_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT1_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT1_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT1_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT1_POWER_LED_GPIO_CLK_ENABLE();

  BOARD_UNIT2_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT2_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT2_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT2_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT2_POWER_LED_GPIO_CLK_ENABLE();

  BOARD_UNIT3_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT3_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT3_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT3_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT3_POWER_LED_GPIO_CLK_ENABLE();

  unit_gpio_output_init(BOARD_UNIT1_POWER_GPIO_PORT,
                        BOARD_UNIT1_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_output_init(BOARD_UNIT1_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT1_PCIE_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_RESET);
  unit_gpio_output_init(BOARD_UNIT1_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT1_POWER_BUTTON_GPIO_PIN,
                        GPIO_MODE_OUTPUT_OD,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_input_init(BOARD_UNIT1_ACTIVITY_LED_GPIO_PORT,
                       BOARD_UNIT1_ACTIVITY_LED_GPIO_PIN,
                       GPIO_PULLUP);
  unit_gpio_input_init(BOARD_UNIT1_POWER_LED_GPIO_PORT,
                       BOARD_UNIT1_POWER_LED_GPIO_PIN,
                       GPIO_PULLUP);

  unit_gpio_output_init(BOARD_UNIT2_POWER_GPIO_PORT,
                        BOARD_UNIT2_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_output_init(BOARD_UNIT2_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT2_PCIE_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_RESET);
  unit_gpio_output_init(BOARD_UNIT2_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT2_POWER_BUTTON_GPIO_PIN,
                        GPIO_MODE_OUTPUT_OD,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_input_init(BOARD_UNIT2_ACTIVITY_LED_GPIO_PORT,
                       BOARD_UNIT2_ACTIVITY_LED_GPIO_PIN,
                       GPIO_PULLUP);
  unit_gpio_input_init(BOARD_UNIT2_POWER_LED_GPIO_PORT,
                       BOARD_UNIT2_POWER_LED_GPIO_PIN,
                       GPIO_PULLUP);

  unit_gpio_output_init(BOARD_UNIT3_POWER_GPIO_PORT,
                        BOARD_UNIT3_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_output_init(BOARD_UNIT3_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT3_PCIE_POWER_GPIO_PIN,
                        GPIO_MODE_OUTPUT_PP,
                        GPIO_NOPULL,
                        GPIO_PIN_RESET);
  unit_gpio_output_init(BOARD_UNIT3_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT3_POWER_BUTTON_GPIO_PIN,
                        GPIO_MODE_OUTPUT_OD,
                        GPIO_NOPULL,
                        GPIO_PIN_SET);
  unit_gpio_input_init(BOARD_UNIT3_ACTIVITY_LED_GPIO_PORT,
                       BOARD_UNIT3_ACTIVITY_LED_GPIO_PIN,
                       GPIO_PULLUP);
  unit_gpio_input_init(BOARD_UNIT3_POWER_LED_GPIO_PORT,
                       BOARD_UNIT3_POWER_LED_GPIO_PIN,
                       GPIO_PULLUP);

  unit_initialized = true;
}

void
unit_init()
{
  unit_gpio_init();

  if (unit_task_handle == NULL)
    {
      unit_task_handle = osThreadNew(unit_task, NULL, &unit_task_attributes);
      if (unit_task_handle == NULL)
        {
          Error_Handler();
        }
    }
}

int
unit_set_power(unit_t unit, bool enable)
{
  switch (unit)
    {
    case UNIT1:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT1_POWER_GPIO_PORT,
                        BOARD_UNIT1_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    case UNIT2:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT2_POWER_GPIO_PORT,
                        BOARD_UNIT2_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    case UNIT3:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT3_POWER_GPIO_PORT,
                        BOARD_UNIT3_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    default:
      return UNIT_ERROR;
    }
}

int
unit_set_pcie_power(unit_t unit, bool enable)
{
  switch (unit)
    {
    case UNIT1:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT1_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT1_PCIE_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    case UNIT2:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT2_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT2_PCIE_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    case UNIT3:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT3_PCIE_POWER_GPIO_PORT,
                        BOARD_UNIT3_PCIE_POWER_GPIO_PIN,
                        enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
      return UNIT_SUCCESS;
    default:
      return UNIT_ERROR;
    }
}

int
unit_set_power_button(unit_t unit, bool enable)
{
  const GPIO_PinState state = enable ? GPIO_PIN_RESET : GPIO_PIN_SET;

  switch (unit)
    {
    case UNIT1:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT1_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT1_POWER_BUTTON_GPIO_PIN,
                        state);
      return UNIT_SUCCESS;
    case UNIT2:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT2_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT2_POWER_BUTTON_GPIO_PIN,
                        state);
      return UNIT_SUCCESS;
    case UNIT3:
      unit_gpio_init();
      HAL_GPIO_WritePin(BOARD_UNIT3_POWER_BUTTON_GPIO_PORT,
                        BOARD_UNIT3_POWER_BUTTON_GPIO_PIN,
                        state);
      return UNIT_SUCCESS;
    default:
      return UNIT_ERROR;
    }
}

int
unit_get_power_led(unit_t unit, bool *on)
{
  if (on == NULL)
    {
      return UNIT_ERROR;
    }

  switch (unit)
    {
    case UNIT1:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT1_POWER_LED_GPIO_PORT,
                             BOARD_UNIT1_POWER_LED_GPIO_PIN)
            == GPIO_PIN_SET;
      return UNIT_SUCCESS;
    case UNIT2:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT2_POWER_LED_GPIO_PORT,
                             BOARD_UNIT2_POWER_LED_GPIO_PIN)
            == GPIO_PIN_SET;
      return UNIT_SUCCESS;
    case UNIT3:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT3_POWER_LED_GPIO_PORT,
                             BOARD_UNIT3_POWER_LED_GPIO_PIN)
            == GPIO_PIN_SET;
      return UNIT_SUCCESS;
    default:
      return UNIT_ERROR;
    }
}

int
unit_get_activity_led(unit_t unit, bool *on)
{
  if (on == NULL)
    {
      return UNIT_ERROR;
    }

  switch (unit)
    {
    case UNIT1:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT1_ACTIVITY_LED_GPIO_PORT,
                             BOARD_UNIT1_ACTIVITY_LED_GPIO_PIN)
            == GPIO_PIN_RESET;
      return UNIT_SUCCESS;
    case UNIT2:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT2_ACTIVITY_LED_GPIO_PORT,
                             BOARD_UNIT2_ACTIVITY_LED_GPIO_PIN)
            == GPIO_PIN_RESET;
      return UNIT_SUCCESS;
    case UNIT3:
      unit_gpio_init();
      *on = HAL_GPIO_ReadPin(BOARD_UNIT3_ACTIVITY_LED_GPIO_PORT,
                             BOARD_UNIT3_ACTIVITY_LED_GPIO_PIN)
            == GPIO_PIN_RESET;
      return UNIT_SUCCESS;
    default:
      return UNIT_ERROR;
    }
}

static void
unit_task(void *argument)
{

  for (;;)
    {
      for (unit_t unit = UNIT1; unit < UNIT_COUNT; unit++)
        {
          bool power_on;
          bool activity_on;
          size_t led_index;

          switch (unit)
            {
            case UNIT1:
              led_index = BOARD_UNIT1_WS2812_INDEX;
              break;
            case UNIT2:
              led_index = BOARD_UNIT2_WS2812_INDEX;
              break;
            case UNIT3:
              led_index = BOARD_UNIT3_WS2812_INDEX;
              break;
            default:
              continue;
            }

          if ((unit_get_power_led(unit, &power_on) == UNIT_SUCCESS)
              && (unit_get_activity_led(unit, &activity_on) == UNIT_SUCCESS))
            {
              ws2812_set_rgb(led_index,
                             power_on ? UNIT_LED_ON : 0U,
                             activity_on ? UNIT_LED_ON : 0U,
                             0U);
            }
        }

      osDelay(UNIT_LED_POLL_INTERVAL_MS);
    }
}
