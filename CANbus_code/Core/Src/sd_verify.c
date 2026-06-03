#include "sd_verify.h"
#include "fatfs.h"
#include "sdmmc.h"
#include "bsp_driver_sd.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
extern SD_HandleTypeDef hsd1;

static void sd_print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 200);
}

uint8_t SD_Verify(void)
{
  char buf[96];
  FATFS *fs;
  FIL fil;
  FRESULT fr;
  UINT bw, br;
  DWORD fre_clust, fre_sect, tot_sect;
  const char *test_path = "test.txt";
  const char *test_data = "STM32H750 SDMMC FatFs verify OK\r\n";
  uint8_t rd_buf[64];
  uint8_t pass = 1;

  sd_print("\r\n[SD] TF Card Verify Start\r\n");

  /* 0. Check card detect (BSP_SD_IsDetected is weak, always returns present) */
  sprintf(buf, "[SD] Card detect: %s\r\n",
          BSP_SD_IsDetected() == SD_PRESENT ? "PRESENT" : "NOT_PRESENT");
  sd_print(buf);

  /* 1. Try to get SD card info before mounting */
  HAL_SD_CardInfoTypeDef card_info;
  if (HAL_SD_GetCardInfo(&hsd1, &card_info) == HAL_OK)
  {
    sprintf(buf, "[SD] Card type=%lu BlockNbr=%lu BlockSize=%lu\r\n",
            card_info.CardType, card_info.BlockNbr, card_info.BlockSize);
    sd_print(buf);
  }
  else
  {
    sd_print("[SD] WARN: HAL_SD_GetCardInfo failed (no card or init error)\r\n");
  }

  /* 2. Mount */
  fr = f_mount(&SDFatFS, SDPath, 1);
  if (fr != FR_OK)
  {
    sprintf(buf, "[SD] FAIL: f_mount error %d\r\n", fr);
    sd_print(buf);
    /* Print HAL SD state for debugging */
    sprintf(buf, "[SD] HAL SD state=%lu error=%lu\r\n",
            hsd1.State, hsd1.ErrorCode);
    sd_print(buf);
    return 0;
  }
  sd_print("[SD] Mount OK\r\n");

  /* 2. Card info */
  fr = f_getfree(SDPath, &fre_clust, &fs);
  if (fr != FR_OK)
  {
    sprintf(buf, "[SD] FAIL: f_getfree error %d\r\n", fr);
    sd_print(buf);
    pass = 0;
    goto unmount;
  }
  tot_sect = (fs->n_fatent - 2) * fs->csize;
  fre_sect = fre_clust * fs->csize;
  sprintf(buf, "[SD] Card: %lu MB total, %lu MB free\r\n",
          tot_sect / 2048, fre_sect / 2048);
  sd_print(buf);

  /* 3. Create / open test file */
  fr = f_open(&fil, test_path, FA_CREATE_ALWAYS | FA_WRITE);
  if (fr != FR_OK)
  {
    sprintf(buf, "[SD] FAIL: f_open(w) error %d\r\n", fr);
    sd_print(buf);
    pass = 0;
    goto unmount;
  }

  /* 4. Write test data */
  fr = f_write(&fil, test_data, strlen(test_data), &bw);
  if (fr != FR_OK || bw != strlen(test_data))
  {
    sprintf(buf, "[SD] FAIL: f_write error %d bw=%u\r\n", fr, bw);
    sd_print(buf);
    pass = 0;
    f_close(&fil);
    goto unmount;
  }
  f_close(&fil);
  sprintf(buf, "[SD] Write OK (%u bytes)\r\n", bw);
  sd_print(buf);

  /* 5. Read back and verify */
  memset(rd_buf, 0, sizeof(rd_buf));
  fr = f_open(&fil, test_path, FA_READ);
  if (fr != FR_OK)
  {
    sprintf(buf, "[SD] FAIL: f_open(r) error %d\r\n", fr);
    sd_print(buf);
    pass = 0;
    goto unmount;
  }
  fr = f_read(&fil, rd_buf, sizeof(rd_buf) - 1, &br);
  f_close(&fil);
  if (fr != FR_OK)
  {
    sprintf(buf, "[SD] FAIL: f_read error %d\r\n", fr);
    sd_print(buf);
    pass = 0;
    goto unmount;
  }
  rd_buf[br] = '\0';

  if (strcmp((char *)rd_buf, test_data) != 0)
  {
    sd_print("[SD] FAIL: Data mismatch\r\n");
    sd_print("[SD]  expected: ");
    sd_print(test_data);
    sd_print("[SD]  got:      ");
    sd_print((char *)rd_buf);
    sd_print("\r\n");
    pass = 0;
  }
  else
  {
    sd_print("[SD] Read verify OK\r\n");
  }

  /* 6. Clean up: delete test file */
  f_unlink(test_path);

unmount:
  f_mount(NULL, SDPath, 0);
  sd_print("[SD] TF Card Verify Done\r\n");
  return pass;
}
