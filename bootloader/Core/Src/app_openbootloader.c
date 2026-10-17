/**
  ******************************************************************************
  * @file    app_openbootloader.c
  * @author  MCD Application Team
  * @brief   OpenBootloader application entry point
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
#include "main.h"
#include "app_openbootloader.h"
#include "usb_interface.h"
#include "flash_interface.h"
#include "openbl_usb_cmd.h"
#include "openbl_core.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static OPENBL_HandleTypeDef USB_Handle;

static OPENBL_OpsTypeDef USB_Ops =
{
  NULL,
  OPENBL_USB_DeInit,
  OPENBL_USB_ProtocolDetection,
  NULL,
  NULL
};

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initialize open Bootloader.
  * @param  None.
  * @retval None.
  */
void OpenBootloader_Init(void)
{
  /* Register USB interfaces */
  USB_Handle.p_Ops = &USB_Ops;

  if (OPENBL_RegisterInterface(&USB_Handle) != SUCCESS)
  {
    Error_Handler();
  }

  /* Initialise interfaces */
  OPENBL_Init();

  /* Initialise memories */
  if (OPENBL_MEM_RegisterMemory(&FLASH_Descriptor) != SUCCESS)
  {
    Error_Handler();
  }
}

/**
  * @brief  DeInitialize open Bootloader.
  * @param  None.
  * @retval None.
  */
void OpenBootloader_DeInit(void)
{
  Common_DisableIrq();
  OPENBL_InterfacesDeInit();
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  NVIC->ICER[0] = 0xFFFFFFFFU;
  NVIC->ICPR[0] = 0xFFFFFFFFU;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
}

/**
  * @brief  This function is used to select which protocol will be used when communicating with the host.
  * @param  None.
  * @retval None.
  */
void OpenBootloader_ProtocolDetection(void)
{
  static uint32_t interface_detected = 0;

  if (interface_detected == 0)
  {
    interface_detected = OPENBL_InterfaceDetection();
  }

  if (interface_detected == 1)
  {
    OPENBL_CommandProcess();
  }
}
