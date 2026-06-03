/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    bsp_driver_sd.c for H7 (based on stm32h743i_eval_sd.c)
 * @brief   This file includes a generic uSD card driver.
 *          To be completed by the user according to the board used for the project.
 * @note    Some functions generated as weak: they can be overridden by
 *          - code in user files
 *          - or BSP code from the FW pack files
 *          if such files are added to the generated project (by the user).
 ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
 ******************************************************************************
 */
/* USER CODE END Header */

/* USER CODE BEGIN FirstSection */
/* can be used to modify / undefine following code or add new definitions */
#include "main.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END FirstSection */
/* Includes ------------------------------------------------------------------*/
#include "bsp_driver_sd.h"

/* Extern variables ---------------------------------------------------------*/

extern SD_HandleTypeDef hsd1;

/* USER CODE BEGIN BeforeInitSection */
/* can be used to modify / undefine following code or add code */
/* USER CODE END BeforeInitSection */
/**
  * @brief  Initializes the SD card device.
  * @retval SD status
  */
__weak uint8_t BSP_SD_Init(void)
{
  uint8_t sd_state = MSD_OK;
  /* Check if the SD card is plugged in the slot */
  if (BSP_SD_IsDetected() != SD_PRESENT)
  {
    /* USER CODE BEGIN BSP_SD_Init_NotPresent */
    {
      extern UART_HandleTypeDef huart2;
      char msg[64];
      snprintf(msg, sizeof(msg), "\r\n[SD] Card NOT present (PA8=%d)\r\n",
               (int)HAL_GPIO_ReadPin(SD_DETECT_GPIO_Port, SD_DETECT_Pin));
      HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);
    }
    /* USER CODE END BSP_SD_Init_NotPresent */
    return MSD_ERROR_SD_NOT_PRESENT;
  }
  /* HAL SD initialization */
  {
    extern UART_HandleTypeDef huart2;
    char msg[80];
    sd_state = HAL_SD_Init(&hsd1);
    snprintf(msg, sizeof(msg), "\r\n[SD] HAL_SD_Init: ret=%d state=%lu err=0x%lX\r\n",
             (int)sd_state, (unsigned long)hsd1.State, (unsigned long)hsd1.ErrorCode);
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);
  }

  /* USER CODE BEGIN BSP_SD_Init_AfterHAL */
  /* STM32H7 HAL may return HAL_ERROR with ErrorCode=0x80000000
     (HAL_SD_ERROR_UNSUPPORTED_FEATURE) while the card is actually
     ready (State=HAL_SD_STATE_READY). Clear the error and proceed. */
  if (sd_state != MSD_OK && hsd1.State == HAL_SD_STATE_READY)
  {
    extern UART_HandleTypeDef huart2;
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    sd_state = MSD_OK;
    HAL_UART_Transmit(&huart2, (uint8_t *)"[SD] UNSUPPORTED_FEATURE cleared\r\n", 35, 200);
  }
  /* USER CODE END BSP_SD_Init_AfterHAL */

  /* Configure SD Bus width (4 bits mode selected) */
  if (sd_state == MSD_OK)
  {
    extern UART_HandleTypeDef huart2;
    char msg[80];
    HAL_StatusTypeDef ret4;

    HAL_UART_Transmit(&huart2, (uint8_t *)"[SD] Trying 4-bit bus...\r\n", 26, 200);
    ret4 = HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B);
    snprintf(msg, sizeof(msg), "[SD] 4-bit result: ret=%d err=0x%lX\r\n",
             (int)ret4, (unsigned long)hsd1.ErrorCode);
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);

    if (ret4 != HAL_OK)
    {
      HAL_StatusTypeDef ret1;
      /* Reinitialize SD to clear peripheral state from failed 4-bit attempt */
      HAL_UART_Transmit(&huart2, (uint8_t *)"[SD] Reinit + 1-bit fallback...\r\n", 33, 200);
      HAL_SD_Init(&hsd1);
      hsd1.ErrorCode = HAL_SD_ERROR_NONE;
      ret1 = HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_1B);
      snprintf(msg, sizeof(msg), "[SD] 1-bit result: ret=%d err=0x%lX\r\n",
               (int)ret1, (unsigned long)hsd1.ErrorCode);
      HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 200);
      if (ret1 != HAL_OK)
      {
        sd_state = MSD_ERROR;
      }
    }
  }

  return sd_state;
}
/* USER CODE BEGIN AfterInitSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END AfterInitSection */

/* USER CODE BEGIN InterruptMode */
/**
  * @brief  Configures Interrupt mode for SD detection pin.
  * @retval Returns 0
  */
__weak uint8_t BSP_SD_ITConfig(void)
{
  /* Code to be updated by the user or replaced by one from the FW pack (in a stmxxxx_sd.c file) */

  return (uint8_t)0;
}

/* USER CODE END InterruptMode */

/* USER CODE BEGIN BeforeReadBlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeReadBlocksSection */
/**
  * @brief  Reads block(s) from a specified address in an SD card, in polling mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  ReadAddr: Address from where data is to be read
  * @param  NumOfBlocks: Number of SD blocks to read
  * @param  Timeout: Timeout for read operation
  * @retval SD status
  */
__weak uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks, Timeout) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeWriteBlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeWriteBlocksSection */
/**
  * @brief  Writes block(s) to a specified address in an SD card, in polling mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  WriteAddr: Address from where data is to be written
  * @param  NumOfBlocks: Number of SD blocks to write
  * @param  Timeout: Timeout for write operation
  * @retval SD status
  */
__weak uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks, Timeout) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeReadDMABlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeReadDMABlocksSection */
/**
  * @brief  Reads block(s) from a specified address in an SD card, in DMA mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  ReadAddr: Address from where data is to be read
  * @param  NumOfBlocks: Number of SD blocks to read
  * @retval SD status
  */
__weak uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
  uint8_t sd_state = MSD_OK;

  /* Read block(s) in DMA transfer mode */
  if (HAL_SD_ReadBlocks_DMA(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeWriteDMABlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeWriteDMABlocksSection */
/**
  * @brief  Writes block(s) to a specified address in an SD card, in DMA mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  WriteAddr: Address from where data is to be written
  * @param  NumOfBlocks: Number of SD blocks to write
  * @retval SD status
  */
__weak uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
  uint8_t sd_state = MSD_OK;

  /* Write block(s) in DMA transfer mode */
  if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeEraseSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeEraseSection */
/**
  * @brief  Erases the specified memory area of the given SD card.
  * @param  StartAddr: Start byte address
  * @param  EndAddr: End byte address
  * @retval SD status
  */
__weak uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_Erase(&hsd1, StartAddr, EndAddr) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeGetCardStateSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeGetCardStateSection */

/**
  * @brief  Gets the current SD card data status.
  * @param  None
  * @retval Data transfer state.
  *          This value can be one of the following values:
  *            @arg  SD_TRANSFER_OK: No data transfer is acting
  *            @arg  SD_TRANSFER_BUSY: Data transfer is acting
  */
__weak uint8_t BSP_SD_GetCardState(void)
{
  HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(&hsd1);
  /* Card is "ready" when idle (0), in STBY (2), or in TRANSFER (1).
     Card is "busy" only during active data transfer or programming. */
  if (card_state == HAL_SD_CARD_SENDING   ||
      card_state == HAL_SD_CARD_RECEIVING  ||
      card_state == HAL_SD_CARD_PROGRAMMING)
  {
    return SD_TRANSFER_BUSY;
  }
  return SD_TRANSFER_OK;
}

/**
  * @brief  Get SD information about specific SD card.
  * @param  CardInfo: Pointer to HAL_SD_CardInfoTypedef structure
  * @retval None
  */
__weak void BSP_SD_GetCardInfo(HAL_SD_CardInfoTypeDef *CardInfo)
{
  /* Get SD card Information */
  HAL_SD_GetCardInfo(&hsd1, CardInfo);
}

/* USER CODE BEGIN BeforeCallBacksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeCallBacksSection */
/**
  * @brief SD Abort callbacks
  * @param hsd: SD handle
  * @retval None
  */
void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_AbortCallback();
}

/**
  * @brief Tx Transfer completed callback
  * @param hsd: SD handle
  * @retval None
  */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_WriteCpltCallback();
}

/**
  * @brief Rx Transfer completed callback
  * @param hsd: SD handle
  * @retval None
  */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_ReadCpltCallback();
}

/* USER CODE BEGIN CallBacksSection_C */
/**
  * @brief BSP SD Abort callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
__weak void BSP_SD_AbortCallback(void)
{

}

/**
  * @brief BSP Tx Transfer completed callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
__weak void BSP_SD_WriteCpltCallback(void)
{

}

/**
  * @brief BSP Rx Transfer completed callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
__weak void BSP_SD_ReadCpltCallback(void)
{

}
/* USER CODE END CallBacksSection_C */

/**
 * @brief  Detects if SD card is correctly plugged in the memory slot or not.
 * @param  None
 * @retval Returns if SD is detected or not
 */
__weak uint8_t BSP_SD_IsDetected(void)
{
  __IO uint8_t status = SD_PRESENT;

  /* USER CODE BEGIN IsDetectedSection */
  /* PA8: input with pull-up. Current hardware reads PA8=1 regardless
     of card insertion. Bypassing card detect to verify SDMMC works first. */
  status = SD_PRESENT;
  /* USER CODE END IsDetectedSection */

  return status;
}

/* USER CODE BEGIN AdditionalCode */
/* user code can be inserted here */
/* USER CODE END AdditionalCode */
