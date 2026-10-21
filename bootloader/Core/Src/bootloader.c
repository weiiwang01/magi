#include "bootloader.h"
#include "header.h"
#include "stm32g0xx_hal.h"
#include "openbl_mem.h"
#include "ws2812.h"

static volatile bool bootloader_exited;
static volatile uint32_t bootloader_exited_at;

void bootloader_error(bootloader_error_t error) {
  for (int i = 1; i < WS2812_MAX_LEDS; i++) {
    if ((error >> (i - 1)) & 1) {
      ws2812_set_rgb(i, 0, 0, 32);
    }
  }
}

bool bootloader_firmware_is_valid(void)
{
  bool valid = HEADER_FIRMWARE->magic[0] == 'm' &&
               HEADER_FIRMWARE->magic[1] == 'a' &&
               HEADER_FIRMWARE->magic[2] == 'g' &&
               HEADER_FIRMWARE->magic[3] == 'i' &&
               HEADER_FIRMWARE->reserved == 0x0000 &&
               HEADER_FIRMWARE->board_version == BOARD_VERSION;
  bootloader_error(valid ? BOOTLOADER_ERROR_NONE : BOOTLOADER_ERROR_INVALID_FIRMWARE);
  return valid;
}

void bootloader_is_enabled(void)
{
  GPIO_InitTypeDef button = {0};
  __HAL_RCC_GPIOD_CLK_ENABLE();
  button.Pin = GPIO_PIN_6;
  button.Mode = GPIO_MODE_INPUT;
  button.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &button);
  HAL_Delay(5U);

  if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_RESET || !bootloader_firmware_is_valid())
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
