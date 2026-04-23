#include "sd_log.h"
#include "fatfs.h"
#include "ff.h"
#include "usart_log.h"

FATFS SDFatFs;  // 文件系统对象
FRESULT res;    // FatFs 返回状态

FRESULT sd_log_init(void) {
  res = f_mount(&SDFatFs, (TCHAR const *)SDPath, 1);
  if (res == FR_OK) {
    xxx_print_log_f("SD Card Mount Success!\r\n");
  } else {
    xxx_print_log_f("SD Card Mount Failed! Error code: %d\r\n", res);
  }
  return res;
}