#include "bootloader.h"
#include <stdbool.h>
#include "stm32g0xx_hal.h"
#include "openbl_mem.h"

static volatile bool bootloader_exited;
static volatile uint32_t bootloader_exited_at;

void bootloader_is_enabled(void)
{
  GPIO_InitTypeDef button = {0};
  __HAL_RCC_GPIOD_CLK_ENABLE();
  button.Pin = GPIO_PIN_6;
  button.Mode = GPIO_MODE_INPUT;
  button.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &button);
  HAL_Delay(5U);

  if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_RESET)
  {
    return;
  }

  OPENBL_MEM_JumpToAddress(FLASH_START_ADDRESS);
}

void bootloader_exit(void)
{
  bootloader_exited_at = HAL_GetTick();
  bootloader_exited = true;
}

void bootloader_process(void)
{
  if (bootloader_exited && (uint32_t)(HAL_GetTick() - bootloader_exited_at) >= 100U)
  {
    NVIC_SystemReset();
  }
}
