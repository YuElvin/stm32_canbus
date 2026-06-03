#include "w25qxx.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

static void qspi_print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 200);
}

static HAL_StatusTypeDef W25QXX_WriteEnable(void)
{
  QSPI_CommandTypeDef cmd = {0};
  cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  cmd.Instruction       = W25Q_CMD_WRITE_ENABLE;
  cmd.AddressMode       = QSPI_ADDRESS_NONE;
  cmd.DataMode          = QSPI_DATA_NONE;
  cmd.DummyCycles       = 0;
  return HAL_QSPI_Command(&hqspi, &cmd, 100);
}

static HAL_StatusTypeDef W25QXX_ReadStatusReg1(uint8_t *sr)
{
  QSPI_CommandTypeDef cmd = {0};
  cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  cmd.Instruction       = W25Q_CMD_READ_STATUS_REG1;
  cmd.AddressMode       = QSPI_ADDRESS_NONE;
  cmd.DataMode          = QSPI_DATA_1_LINE;
  cmd.DummyCycles       = 0;
  cmd.NbData            = 1;

  if (HAL_QSPI_Command(&hqspi, &cmd, 100) != HAL_OK)
    return HAL_ERROR;
  return HAL_QSPI_Receive(&hqspi, sr, 100);
}

static HAL_StatusTypeDef W25QXX_WaitBusy(uint32_t timeout_ms)
{
  uint32_t tick = HAL_GetTick();
  uint8_t sr;

  do {
    if (W25QXX_ReadStatusReg1(&sr) != HAL_OK)
      return HAL_ERROR;
    if ((sr & W25Q_SR_BUSY) == 0)
      return HAL_OK;
  } while ((HAL_GetTick() - tick) < timeout_ms);

  return HAL_TIMEOUT;
}

HAL_StatusTypeDef W25QXX_ReadJEDEC(uint8_t *manufacturer, uint8_t *memType, uint8_t *capacity)
{
  QSPI_CommandTypeDef cmd = {0};
  uint8_t buf[3] = {0};

  cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  cmd.Instruction       = W25Q_CMD_READ_JEDEC_ID;
  cmd.AddressMode       = QSPI_ADDRESS_NONE;
  cmd.DataMode          = QSPI_DATA_1_LINE;
  cmd.DummyCycles       = 0;
  cmd.NbData            = 3;

  if (HAL_QSPI_Command(&hqspi, &cmd, 100) != HAL_OK)
    return HAL_ERROR;

  if (HAL_QSPI_Receive(&hqspi, buf, 100) != HAL_OK)
    return HAL_ERROR;

  *manufacturer = buf[0];
  *memType      = buf[1];
  *capacity     = buf[2];
  return HAL_OK;
}

HAL_StatusTypeDef W25QXX_EraseSector(uint32_t addr)
{
  QSPI_CommandTypeDef cmd = {0};
  uint8_t sr;
  char dbg[48];

  /* Read SR before WriteEnable */
  W25QXX_ReadStatusReg1(&sr);
  sprintf(dbg, " SR_before=%02X", sr);
  qspi_print(dbg);

  if (W25QXX_WriteEnable() != HAL_OK)
  {
    qspi_print(" WREN_FAIL");
    return HAL_ERROR;
  }

  /* Verify WEL bit set */
  W25QXX_ReadStatusReg1(&sr);
  sprintf(dbg, " SR_after_WREN=%02X", sr);
  qspi_print(dbg);

  if ((sr & W25Q_SR_WEL) == 0)
  {
    qspi_print(" WEL_NOT_SET");
    return HAL_ERROR;
  }

  cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  cmd.Instruction       = W25Q_CMD_SECTOR_ERASE;
  cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
  cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
  cmd.Address           = addr;
  cmd.DataMode          = QSPI_DATA_NONE;
  cmd.DummyCycles       = 0;

  if (HAL_QSPI_Command(&hqspi, &cmd, 100) != HAL_OK)
  {
    qspi_print(" CMD_FAIL");
    return HAL_ERROR;
  }

  if (W25QXX_WaitBusy(4000) != HAL_OK)
  {
    W25QXX_ReadStatusReg1(&sr);
    sprintf(dbg, " BUSY_TIMEOUT SR=%02X", sr);
    qspi_print(dbg);
    return HAL_ERROR;
  }
  return HAL_OK;
}

HAL_StatusTypeDef W25QXX_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
  QSPI_CommandTypeDef cmd = {0};

  while (len > 0)
  {
    /* Calculate bytes remaining in current page */
    uint32_t page_remaining = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);
    uint32_t chunk = (len < page_remaining) ? len : page_remaining;

    if (W25QXX_WriteEnable() != HAL_OK)
      return HAL_ERROR;

    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = W25Q_CMD_PAGE_PROGRAM;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.Address           = addr;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = chunk;

    if (HAL_QSPI_Command(&hqspi, &cmd, 100) != HAL_OK)
      return HAL_ERROR;

    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)data, 1000) != HAL_OK)
      return HAL_ERROR;

    if (W25QXX_WaitBusy(1000) != HAL_OK)
      return HAL_ERROR;

    addr += chunk;
    data += chunk;
    len  -= chunk;
  }
  return HAL_OK;
}

HAL_StatusTypeDef W25QXX_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
  QSPI_CommandTypeDef cmd = {0};

  cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  cmd.Instruction       = W25Q_CMD_READ_DATA;
  cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
  cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
  cmd.Address           = addr;
  cmd.DataMode          = QSPI_DATA_1_LINE;
  cmd.DummyCycles       = 0;
  cmd.NbData            = len;

  if (HAL_QSPI_Command(&hqspi, &cmd, 100) != HAL_OK)
    return HAL_ERROR;

  return HAL_QSPI_Receive(&hqspi, data, 1000);
}

uint8_t W25QXX_Verify(void)
{
  char buf[80];
  uint8_t mfr, type, cap;
  uint8_t pass = 1;

  /* 1. Read JEDEC ID */
  qspi_print("\r\n[QSPI] W25Q128 Verify Start\r\n");

  if (W25QXX_ReadJEDEC(&mfr, &type, &cap) != HAL_OK)
  {
    qspi_print("[QSPI] FAIL: Cannot read JEDEC ID\r\n");
    return 0;
  }
  sprintf(buf, "[QSPI] JEDEC ID: %02X %02X %02X\r\n", mfr, type, cap);
  qspi_print(buf);

  if (mfr != W25Q128_MANUFACTURER_ID || type != W25Q128_MEMORY_TYPE || cap != W25Q128_CAPACITY_ID)
  {
    sprintf(buf, "[QSPI] FAIL: Expected EF 40 18, got %02X %02X %02X\r\n", mfr, type, cap);
    qspi_print(buf);
    return 0;
  }
  qspi_print("[QSPI] JEDEC ID OK\r\n");

  /* 2. Use last sector (addr = 0xFF000) for test to avoid overwriting anything important */
  uint32_t test_addr = 0x00FF0000;
  uint8_t wr_buf[256];
  uint8_t rd_buf[256];

  /* Fill test pattern */
  for (int i = 0; i < 256; i++)
    wr_buf[i] = (uint8_t)(i & 0xFF);

  /* 3. Sector erase */
  sprintf(buf, "[QSPI] Erasing sector at 0x%06lX ...", test_addr);
  qspi_print(buf);
  if (W25QXX_EraseSector(test_addr) != HAL_OK)
  {
    qspi_print(" FAIL\r\n");
    return 0;
  }
  qspi_print(" OK\r\n");

  /* 4. Read back erased sector (should be all 0xFF) */
  if (W25QXX_Read(test_addr, rd_buf, 256) != HAL_OK)
  {
    qspi_print("[QSPI] FAIL: Read after erase\r\n");
    return 0;
  }
  for (int i = 0; i < 256; i++)
  {
    if (rd_buf[i] != 0xFF)
    {
      sprintf(buf, "[QSPI] FAIL: Erase verify byte[%d]=%02X (expected FF)\r\n", i, rd_buf[i]);
      qspi_print(buf);
      pass = 0;
      break;
    }
  }
  if (pass)
    qspi_print("[QSPI] Erase verify OK (all 0xFF)\r\n");

  /* 5. Page program */
  qspi_print("[QSPI] Programming 256 bytes ...");
  if (W25QXX_Write(test_addr, wr_buf, 256) != HAL_OK)
  {
    qspi_print(" FAIL\r\n");
    return 0;
  }
  qspi_print(" OK\r\n");

  /* 6. Read back and compare */
  memset(rd_buf, 0, sizeof(rd_buf));
  if (W25QXX_Read(test_addr, rd_buf, 256) != HAL_OK)
  {
    qspi_print("[QSPI] FAIL: Read after program\r\n");
    return 0;
  }
  pass = 1;
  for (int i = 0; i < 256; i++)
  {
    if (rd_buf[i] != wr_buf[i])
    {
      sprintf(buf, "[QSPI] FAIL: Data mismatch byte[%d] wr=%02X rd=%02X\r\n", i, wr_buf[i], rd_buf[i]);
      qspi_print(buf);
      pass = 0;
      break;
    }
  }
  if (pass)
    qspi_print("[QSPI] Program verify OK (256 bytes match)\r\n");

  /* 7. Clean up: erase the test sector */
  W25QXX_EraseSector(test_addr);

  qspi_print("[QSPI] W25Q128 Verify Done\r\n");
  return pass;
}
