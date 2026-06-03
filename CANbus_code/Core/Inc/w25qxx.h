#ifndef __W25QXX_H__
#define __W25QXX_H__

#include "main.h"
#include "quadspi.h"

/* W25Q128 commands */
#define W25Q_CMD_WRITE_ENABLE       0x06
#define W25Q_CMD_WRITE_DISABLE      0x04
#define W25Q_CMD_READ_STATUS_REG1   0x05
#define W25Q_CMD_READ_JEDEC_ID      0x9F
#define W25Q_CMD_SECTOR_ERASE       0x20   /* 4KB, 3-byte addr */
#define W25Q_CMD_BLOCK_ERASE_32K    0x52
#define W25Q_CMD_BLOCK_ERASE_64K    0xD8
#define W25Q_CMD_CHIP_ERASE         0xC7
#define W25Q_CMD_PAGE_PROGRAM       0x02   /* 256B page, 3-byte addr */
#define W25Q_CMD_READ_DATA          0x03   /* 3-byte addr */

/* Status register bits */
#define W25Q_SR_BUSY                (1 << 0)
#define W25Q_SR_WEL                 (1 << 1)

/* Expected JEDEC ID for W25Q128 */
#define W25Q128_MANUFACTURER_ID     0xEF
#define W25Q128_MEMORY_TYPE         0x40
#define W25Q128_CAPACITY_ID         0x18

/* Sector size */
#define W25Q_SECTOR_SIZE            4096
#define W25Q_PAGE_SIZE              256

HAL_StatusTypeDef W25QXX_ReadJEDEC(uint8_t *manufacturer, uint8_t *memType, uint8_t *capacity);
HAL_StatusTypeDef W25QXX_EraseSector(uint32_t addr);
HAL_StatusTypeDef W25QXX_Write(uint32_t addr, const uint8_t *data, uint32_t len);
HAL_StatusTypeDef W25QXX_Read(uint32_t addr, uint8_t *data, uint32_t len);
uint8_t W25QXX_Verify(void);

#endif /* __W25QXX_H__ */
