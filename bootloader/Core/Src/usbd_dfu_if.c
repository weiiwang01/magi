/**
  ******************************************************************************
  * @file           USB_Device/DFU_Standalone/USB_Device/App/usbd_dfu_if.c
  * @author         MCD Application Team
  * @brief          Memory management layer
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019-2020 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usbd_dfu_if.h"
#include "openbl_usb_cmd.h"

#include "flash_interface.h"
#include "bootloader.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

static void EncodeAddress(uint32_t address, uint8_t *bytes)
{
  for (uint32_t i = 0U; i < 4U; ++i)
  {
    bytes[i] = (uint8_t)(address >> (8U * i));
  }
}

static USBD_DFU_HandleTypeDef *DfuState(void)
{
  return hUsbDeviceFS.pClassDataCmsit[hUsbDeviceFS.classId];
}

/**
  * @brief  Memory initialization routine.
  * @retval USBD_OK if operation is successful, MAL_FAIL else.
  */
static uint16_t USB_DFU_If_Init(void)
{
  DfuState()->data_ptr = FLASH_START_ADDRESS;
  return 0;
}

/**
  * @brief  De-Initializes Memory
  * @retval USBD_OK if operation is successful, MAL_FAIL else
  */
static uint16_t USB_DFU_If_DeInit(void)
{
  return 0;
}

/**
  * @brief  Erase sector.
  * @param  Add: Address of sector to be erased.
  * @retval 0 if operation is successful, MAL_FAIL else.
  */
static uint16_t USB_DFU_If_Erase(uint32_t address)
{
  if (!flash_range_valid(address, 1U))
  {
    return USBD_FAIL;
  }
  return OPENBL_USB_EraseMemory(address) == 0U ? USBD_OK : USBD_FAIL;
}

/**
  * @brief  Memory write routine.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be written (in bytes).
  * @retval USBD_OK if operation is successful, MAL_FAIL else.
  */
static uint16_t USB_DFU_If_Write(uint8_t *src, uint8_t *dest, uint32_t length)
{
  const uint32_t address = (uint32_t)(uintptr_t)dest;
  uint8_t encoded[4];
  if (!flash_range_valid(address, length) || length == 0U || (address & 7U) != 0U)
  {
    return USBD_FAIL;
  }
  EncodeAddress(address, encoded);
  OPENBL_USB_WriteMemory(src, encoded, length);
  return USBD_OK;
}

/**
  * @brief  Memory read routine.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be read (in bytes).
  * @retval Pointer to the physical address where data should be read.
  */
static uint8_t *USB_DFU_If_Read(uint8_t *src, uint8_t *dest, uint32_t length)
{
  const uint32_t address = (uint32_t)(uintptr_t)src;
  uint8_t encoded[4];
  if (!flash_range_valid(address, length) || length > USBD_DFU_XFER_SIZE)
  {
    return NULL;
  }
  EncodeAddress(address, encoded);
  return OPENBL_USB_ReadMemory(encoded, dest, length);
}

/**
  * @brief  Get status routine
  * @param  Add: Address to be read from
  * @param  Cmd: Number of data to be read (in bytes)
  * @param  pBuffer: used for returning the time necessary for a program or an erase operation
  * @retval USBD_OK if operation is successful
  */
static uint16_t USB_DFU_If_GetStatus(uint32_t address, uint8_t command, uint8_t *status)
{
  status[1] = 100U;
  status[2] = 0U;
  status[3] = 0U;
  return USBD_OK;
}

static uint16_t Dfu_Leave(uint32_t address)
{
  bootloader_exit();
  return USBD_OK;
}

USBD_DFU_MediaTypeDef USBD_DFU_Flash_fops = {
  .pStrDesc = (const uint8_t *)"@Application Flash /0x08010000/224*002Kg",
  .Init = USB_DFU_If_Init,
  .DeInit = USB_DFU_If_DeInit,
  .Erase = USB_DFU_If_Erase,
  .Write = USB_DFU_If_Write,
  .Read = USB_DFU_If_Read,
  .GetStatus = USB_DFU_If_GetStatus,
  .LeaveDFU = Dfu_Leave
};
