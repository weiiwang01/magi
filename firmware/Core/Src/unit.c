#include "unit.h"

#include <errno.h>

#include "board.h"
#include "cmsis_os.h"
#include "console.h"
#include "ws2812.h"

#define UNIT_POLL_INTERVAL_MS 50U

#define UNIT_GPIO(n, signal)                                                  \
  {                                                                           \
    .port = BOARD_UNIT##n##_##signal##_GPIO_PORT,                             \
    .pin = BOARD_UNIT##n##_##signal##_GPIO_PIN,                               \
  }
#define UNIT_INSTANCE(n)                                                      \
  {                                                                           \
    .type = UNIT_TYPE_CM5,                                                    \
    .power = UNIT_GPIO(n, POWER),                                             \
    .pcie_power = UNIT_GPIO(n, PCIE_POWER),                                   \
    .power_button = UNIT_GPIO(n, POWER_BUTTON),                               \
    .rpiboot = UNIT_GPIO(n, RPIBOOT),                                         \
    .activity_led = UNIT_GPIO(n, ACTIVITY_LED),                               \
    .power_led = UNIT_GPIO(n, POWER_LED),                                     \
    .sense_3v3 = UNIT_GPIO(n, 3V3_SENSE),                                     \
    .ws2812_index = BOARD_UNIT##n##_WS2812_INDEX,                             \
  }

static unit_t units[UNIT_COUNT]
    = { UNIT_INSTANCE(1), UNIT_INSTANCE(2), UNIT_INSTANCE(3) };
unit_t *unit_1 = &units[0];
unit_t *unit_2 = &units[1];
unit_t *unit_3 = &units[2];

static bool unit_initialized;
static osThreadId_t unit_task_handle;
static const osThreadAttr_t unit_task_attributes = {
  .name = "units",
  .priority = (osPriority_t)osPriorityLow,
  .stack_size = 128U * 4U,
};

static void unit_task(void *argument);

static void
unit_gpio_output_init(const unit_gpio_t *gpio,
                      uint32_t mode,
                      GPIO_PinState state)
{
  HAL_GPIO_WritePin(gpio->port, gpio->pin, state);
  GPIO_InitTypeDef config = {
    .Pin = gpio->pin,
    .Mode = mode,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(gpio->port, &config);
}

static void
unit_gpio_input_init(const unit_gpio_t *gpio, uint32_t pull)
{
  GPIO_InitTypeDef config
      = { .Pin = gpio->pin, .Mode = GPIO_MODE_INPUT, .Pull = pull };
  HAL_GPIO_Init(gpio->port, &config);
}

static void
unit_rail_disable(unit_t *unit)
{
  uint16_t pin = unit->sense_3v3.pin;
  CLEAR_BIT(EXTI->IMR1, pin);
  CLEAR_BIT(EXTI->FTSR1, pin);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(pin);
  console_disable(unit->console);
  unit_gpio_input_init(&unit->activity_led, GPIO_NOPULL);
}

static void
unit_rail_poll(unit_t *unit)
{
  const unit_gpio_t *sense = &unit->sense_3v3;
  taskENTER_CRITICAL();
  bool armed = (EXTI->IMR1 & sense->pin) != 0;
  bool high = HAL_GPIO_ReadPin(sense->port, sense->pin) == GPIO_PIN_SET;
  if (armed)
    {
      if (!high || __HAL_GPIO_EXTI_GET_FALLING_IT(sense->pin))
        {
          unit_rail_disable(unit);
        }
    }
  else if (high)
    {
      __HAL_GPIO_EXTI_CLEAR_FALLING_IT(sense->pin);
      SET_BIT(EXTI->FTSR1, sense->pin);
      SET_BIT(EXTI->IMR1, sense->pin);
      unit_gpio_input_init(&unit->activity_led, GPIO_PULLUP);
      console_enable(unit->console);
      if (HAL_GPIO_ReadPin(sense->port, sense->pin) == GPIO_PIN_RESET
          || __HAL_GPIO_EXTI_GET_FALLING_IT(sense->pin))
        {
          unit_rail_disable(unit);
        }
    }
  taskEXIT_CRITICAL();
}

void
unit_exti_irq()
{
  if (!unit_initialized)
    {
      return;
    }
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (size_t i = 0; i < UNIT_COUNT; ++i)
    {
      unit_t *unit = &units[i];
      uint16_t pin = unit->sense_3v3.pin;
      if ((EXTI->IMR1 & pin) && __HAL_GPIO_EXTI_GET_FALLING_IT(pin))
        {
          unit_rail_disable(unit);
        }
    }
  __set_PRIMASK(primask);
}

static void
unit_gpio_init()
{
  if (unit_initialized)
    {
      return;
    }

  unit_1->console = console_unit_1;
  unit_2->console = console_unit_2;
  unit_3->console = console_unit_3;

  BOARD_UNIT1_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT1_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT1_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT1_RPIBOOT_GPIO_CLK_ENABLE();
  BOARD_UNIT1_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT1_POWER_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT1_3V3_SENSE_GPIO_CLK_ENABLE();

  BOARD_UNIT2_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT2_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT2_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT2_RPIBOOT_GPIO_CLK_ENABLE();
  BOARD_UNIT2_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT2_POWER_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT2_3V3_SENSE_GPIO_CLK_ENABLE();

  BOARD_UNIT3_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT3_PCIE_POWER_GPIO_CLK_ENABLE();
  BOARD_UNIT3_POWER_BUTTON_GPIO_CLK_ENABLE();
  BOARD_UNIT3_RPIBOOT_GPIO_CLK_ENABLE();
  BOARD_UNIT3_ACTIVITY_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT3_POWER_LED_GPIO_CLK_ENABLE();
  BOARD_UNIT3_3V3_SENSE_GPIO_CLK_ENABLE();

  BOARD_BUTTON_GPIO_CLK_ENABLE();
  const unit_gpio_t button = { BOARD_BUTTON_GPIO_PORT, BOARD_BUTTON_GPIO_PIN };
  unit_gpio_input_init(&button, GPIO_NOPULL);

  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (size_t i = 0; i < UNIT_COUNT; ++i)
    {
      unit_t *unit = &units[i];
      unit_gpio_output_init(&unit->power, GPIO_MODE_OUTPUT_PP, GPIO_PIN_SET);
      unit_gpio_output_init(&unit->pcie_power,
                            GPIO_MODE_OUTPUT_PP,
                            unit->type == UNIT_TYPE_CM4 ? GPIO_PIN_SET
                                                        : GPIO_PIN_RESET);
      unit_gpio_output_init(
          &unit->power_button, GPIO_MODE_OUTPUT_OD, GPIO_PIN_SET);
      unit_gpio_output_init(&unit->rpiboot, GPIO_MODE_OUTPUT_OD, GPIO_PIN_SET);
      unit_gpio_input_init(&unit->power_led, GPIO_NOPULL);
      GPIO_InitTypeDef sense = {
        .Pin = unit->sense_3v3.pin,
        .Mode = GPIO_MODE_IT_FALLING,
        .Pull = GPIO_PULLDOWN,
      };
      HAL_GPIO_Init(unit->sense_3v3.port, &sense);
      unit_rail_disable(unit);
      __HAL_GPIO_EXTI_CLEAR_RISING_IT(unit->sense_3v3.pin);
    }
  HAL_NVIC_SetPriority(BOARD_UNIT_3V3_SENSE_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(BOARD_UNIT_3V3_SENSE_IRQn);

  unit_initialized = true;
  __set_PRIMASK(primask);
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
unit_set_type(unit_t *unit, unit_type_t type)
{
  if (type != UNIT_TYPE_CM5 && type != UNIT_TYPE_CM4)
    {
      return EINVAL;
    }
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (unit->type != type)
    {
      unit->type = type;
      if (unit_initialized)
        {
          unit_set_pcie_power(unit,
                              type == UNIT_TYPE_CM4 && unit_get_power(unit));
        }
    }
  __set_PRIMASK(primask);
  return 0;
}

unit_type_t
unit_get_type(const unit_t *unit)
{
  return unit->type;
}

void
unit_set_power(unit_t *unit, bool enable)
{
  taskENTER_CRITICAL();
  HAL_GPIO_WritePin(unit->power.port,
                    unit->power.pin,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
  if (unit->type == UNIT_TYPE_CM4)
    {
      unit_set_pcie_power(unit, enable);
    }
  taskEXIT_CRITICAL();
}

bool
unit_get_power(const unit_t *unit)
{
  return (unit->power.port->ODR & unit->power.pin) != 0U;
}

void
unit_set_pcie_power(unit_t *unit, bool enable)
{
  HAL_GPIO_WritePin(unit->pcie_power.port,
                    unit->pcie_power.pin,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void
unit_set_power_button(unit_t *unit, bool enable)
{
  HAL_GPIO_WritePin(unit->power_button.port,
                    unit->power_button.pin,
                    enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static bool
unit_get_button_pressed(void)
{
  return HAL_GPIO_ReadPin(BOARD_BUTTON_GPIO_PORT, BOARD_BUTTON_GPIO_PIN)
         == GPIO_PIN_RESET;
}

void
unit_set_power_button_press(unit_t *unit, uint32_t ms)
{
  taskENTER_CRITICAL();
  timeout_init(&unit->power_button_press, ms);
  unit->power_button_pressed = ms != 0U;
  bool pressed = unit_get_button_pressed();
  unit_set_power_button(unit, pressed || unit->power_button_pressed);
  taskEXIT_CRITICAL();
}

uint32_t
unit_get_power_button_press(const unit_t *unit)
{
  taskENTER_CRITICAL();
  uint32_t remaining = unit->power_button_pressed
                           ? timeout_remaining(&unit->power_button_press)
                           : 0U;
  taskEXIT_CRITICAL();
  return remaining;
}

void
unit_set_rpiboot(unit_t *unit, bool enable)
{
  HAL_GPIO_WritePin(unit->rpiboot.port,
                    unit->rpiboot.pin,
                    enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool
unit_get_rpiboot(const unit_t *unit)
{
  return (unit->rpiboot.port->ODR & unit->rpiboot.pin) == 0U;
}

bool
unit_get_3v3(const unit_t *unit)
{
  return HAL_GPIO_ReadPin(unit->sense_3v3.port, unit->sense_3v3.pin)
         == GPIO_PIN_SET;
}

bool
unit_get_power_led(const unit_t *unit)
{
  return unit_get_3v3(unit)
         && HAL_GPIO_ReadPin(unit->power_led.port, unit->power_led.pin)
                == (unit->type == UNIT_TYPE_CM4 ? GPIO_PIN_RESET
                                                : GPIO_PIN_SET);
}

bool
unit_get_activity_led(const unit_t *unit)
{
  return unit_get_3v3(unit)
         && HAL_GPIO_ReadPin(unit->activity_led.port, unit->activity_led.pin)
                == (unit->type == UNIT_TYPE_CM4 ? GPIO_PIN_RESET
                                                : GPIO_PIN_SET);
}

static void
unit_task(void *argument)
{

  for (;;)
    {
      bool pressed = unit_get_button_pressed();
      for (size_t i = 0; i < UNIT_COUNT; ++i)
        {
          unit_t *unit = &units[i];
          taskENTER_CRITICAL();
          if (unit->power_button_pressed
              && timeout_expired(&unit->power_button_press))
            {
              unit->power_button_pressed = false;
            }
          unit_set_power_button(unit, pressed || unit->power_button_pressed);
          taskEXIT_CRITICAL();
          unit_rail_poll(unit);

          bool power_on = unit_get_power_led(unit);
          bool activity_on = unit_get_activity_led(unit);
          ws2812_set_rgb(unit->ws2812_index,
                         power_on ? 24 : 0U,
                         activity_on ? 12 : 0U,
                         0U);
        }
      osDelay(UNIT_POLL_INTERVAL_MS);
    }
}
