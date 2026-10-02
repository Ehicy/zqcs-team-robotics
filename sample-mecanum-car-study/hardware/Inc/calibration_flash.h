#ifndef CALIBRATION_FLASH_H
#define CALIBRATION_FLASH_H
#include <stdint.h>
/* 存储算法与实际FPEC分离，主机测试可以注入断电/写入失败。 */
uint16_t calibration_flash_read(uint32_t address);
void calibration_flash_begin(void);
void calibration_flash_end(void);
uint8_t calibration_flash_erase(uint32_t page);
uint8_t calibration_flash_program(uint32_t address, uint16_t value);
#endif
