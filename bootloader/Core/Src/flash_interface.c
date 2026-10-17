/**
  ******************************************************************************
  * @file    flash_interface.c
  * @author  MCD Application Team
  * @brief   Contains FLASH access functions
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
#include "platform.h"
#include "openbl_mem.h"
#include "app_openbootloader.h"
#include "common_interface.h"
#include "flash_interface.h"
#include <string.h>

static void OPENBL_FLASH_ProgramDoubleWord(uint32_t Address, uint64_t Data);

OPENBL_MemoryTypeDef FLASH_Descriptor =
{
  FLASH_START_ADDRESS,
  FLASH_END_ADDRESS,
  FLASH_END_ADDRESS - FLASH_START_ADDRESS,
  FLASH_AREA,
  OPENBL_FLASH_Read,
  OPENBL_FLASH_Write,
  NULL,
  NULL,
  OPENBL_FLASH_JumpToAddress,
  OPENBL_FLASH_MassErase,
  OPENBL_FLASH_Erase
};

/**
  * @brief  Unlock the FLASH control register access.
  * @retval None.
  */
void OPENBL_FLASH_Unlock(void)
{
  HAL_FLASH_Unlock();
}

/**
  * @brief  Lock the FLASH control register access.
  * @retval None.
  */
void OPENBL_FLASH_Lock(void)
{
  HAL_FLASH_Lock();
}

/**
  * @brief  This function is used to read data from a given address.
  * @param  Address The address to be read.
  * @retval Returns the read value.
  */
uint8_t OPENBL_FLASH_Read(uint32_t Address)
{
  return (*(uint8_t *)(Address));
}


/**
  * @brief  This function is used to write data in FLASH memory.
  * @param  Address The address where that data will be written.
  * @param  Data The data to be written.
  * @param  DataLength The length of the data to be written.
  * @retval None.
  */
void OPENBL_FLASH_Write(uint32_t Address, uint8_t *Data, uint32_t DataLength)
{
  uint32_t index  = 0U;
  if (Data == NULL || DataLength == 0U || (Address & 7U) != 0U ||
      !flash_range_valid(Address, DataLength))
  {
    return;
  }

  OPENBL_FLASH_Unlock();
  for (index = 0U; index < DataLength; (index += 8U))
  {
    uint64_t data = UINT64_MAX;
    uint32_t count = DataLength - index < 8U ? DataLength - index : 8U;
    memcpy(&data, Data + index, count);
    OPENBL_FLASH_ProgramDoubleWord(Address + index, data);
  }

  OPENBL_FLASH_Lock();
}

/**
  * @brief  This function is used to jump to a given address.
  * @param  Address The address where the function will jump.
  * @retval None.
  */
void OPENBL_FLASH_JumpToAddress(uint32_t Address)
{
  Function_Pointer jump_to_address;

  OpenBootloader_DeInit();

  jump_to_address = (Function_Pointer)(*(__IO uint32_t *)(Address + 4U));

  Common_EnableIrq();

  /* Initialize user application's stack pointer */
  Common_SetMsp(*(__IO uint32_t *)Address);

  jump_to_address();
}

/**
  * @brief  This function is used to start FLASH mass erase operation.
  * @param  *p_Data Pointer to the buffer that contains mass erase operation options.
  * @param  DataLength Size of the Data buffer.
  * @note   This target ignores the options and erases only application pages.
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: Mass erase operation done
  *          - ERROR:   Mass erase operation failed or the value of one parameter is not ok
  */
ErrorStatus OPENBL_FLASH_MassErase(uint8_t *p_Data, uint32_t DataLength)
{
  uint16_t page_list[2] = {1U, 0U};
  (void)p_Data;
  (void)DataLength;
  for (uint32_t page = (FLASH_START_ADDRESS - FLASH_BASE) / FLASH_PAGE_SIZE;
       page < (FLASH_END_ADDRESS - FLASH_BASE) / FLASH_PAGE_SIZE; ++page)
  {
    page_list[1] = (uint16_t)page;
    if (OPENBL_FLASH_Erase((uint8_t *)page_list, sizeof(page_list)) != SUCCESS)
    {
      return ERROR;
    }
  }
  return SUCCESS;
}

/**
  * @brief  This function is used to erase the specified FLASH pages.
  * @param  *p_Data Pointer to the buffer that contains erase operation options.
  * @param  DataLength Size of the Data buffer.
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: Erase operation done
  *          - ERROR:   Erase operation failed or the value of one parameter is not ok
  */
ErrorStatus OPENBL_FLASH_Erase(uint8_t *p_Data, uint32_t DataLength)
{
  uint32_t counter      = 0U;
  uint32_t pages_number = 0U;
  uint32_t page_error   = 0U;
  uint32_t errors       = 0U;
  ErrorStatus status   = SUCCESS;
  FLASH_EraseInitTypeDef erase_init_struct;

  if (FLASH_BANK_NB != 2U || (FLASH->OPTR & FLASH_OPTR_nSWAP_BANK) == 0U)
  {
    return ERROR;
  }

  if (p_Data == NULL || DataLength < 4U)
  {
    return ERROR;
  }
  pages_number = (uint32_t)p_Data[0] | ((uint32_t)p_Data[1] << 8);
  if (pages_number == 0U || pages_number > (DataLength - 2U) / 2U)
  {
    return ERROR;
  }
  for (counter = 0U; counter < pages_number; ++counter)
  {
    uint32_t page = p_Data[2U + 2U * counter] | ((uint32_t)p_Data[3U + 2U * counter] << 8);
    if (page < (FLASH_START_ADDRESS - FLASH_BASE) / FLASH_PAGE_SIZE ||
        page >= (FLASH_END_ADDRESS - FLASH_BASE) / FLASH_PAGE_SIZE)
    {
      return ERROR;
    }
  }
  /* Unlock the flash memory for erase operation */
  OPENBL_FLASH_Unlock();

  /* Clear error programming flags */
  WRITE_REG(FLASH->SR, FLASH_SR_ERRORS);

  pages_number  = (uint32_t)(*(uint16_t *)(p_Data));
  p_Data       += 2;

  erase_init_struct.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init_struct.NbPages   = 1U;

  for (counter = 0U; ((counter < pages_number) && (counter < (DataLength / 2U))); counter++)
  {
    erase_init_struct.Page = ((uint32_t)(*(uint16_t *)(p_Data)));

    if (erase_init_struct.Page <= 127)
    {
      erase_init_struct.Banks = FLASH_BANK_1;
    }
    else if (erase_init_struct.Page <= 255)
    {
      erase_init_struct.Banks = FLASH_BANK_2;
    }
    else
    {
      status = ERROR;
    }

    if (status != ERROR)
    {
      if (HAL_FLASHEx_Erase(&erase_init_struct, &page_error) != HAL_OK)
      {
        errors++;
      }
      else
      {
        /* Nothing to do */
      }
    }
    else
    {
      /* Reset the status for next erase operation */
      status = SUCCESS;
    }

    p_Data += 2;
  }

  if (errors > 0)
  {
    status = ERROR;
  }
  else
  {
    status = SUCCESS;
  }

  OPENBL_FLASH_Lock();
  return status;
}

/**
  * @brief  Program double word at a specified FLASH address.
  * @param  Address specifies the address to be programmed.
  * @param  Data specifies the data to be programmed.
  * @retval None.
  */
static void OPENBL_FLASH_ProgramDoubleWord(uint32_t Address, uint64_t Data)
{
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, Address, Data);
}
