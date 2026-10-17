/**
  ******************************************************************************
  * @file    usb_interface.c
  * @author  MCD Application Team
  * @brief   Contains USB protocol commands
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usb_interface.h"
#include "usbd_core.h"
#include "flash_interface.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/


/* Private function prototypes -----------------------------------------------*/
/* Exported functions --------------------------------------------------------*/

/**
 * @brief  This function is used to detect if there is any activity on USB protocol.
 * @retval None.
 */
uint8_t OPENBL_USB_ProtocolDetection(void)
{
  uint8_t detected;

  if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED)
  {
    detected = 1;
  }
  else
  {
    detected = 0;
  }

  return detected;
}

void OPENBL_USB_DeInit(void)
{
  if (hUsbDeviceFS.pData != NULL)
  {
    USBD_DeInit(&hUsbDeviceFS);
  }
}

uint32_t OPENBL_USB_GetPage(uint32_t Address)
{
  return flash_range_valid(Address, 1U) ? (Address - FLASH_BASE) / FLASH_PAGE_SIZE : UINT32_MAX;
}
