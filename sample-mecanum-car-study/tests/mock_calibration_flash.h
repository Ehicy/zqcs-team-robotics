#ifndef MOCK_CALIBRATION_FLASH_H
#define MOCK_CALIBRATION_FLASH_H
#include <stdint.h>
extern unsigned mock_flash_programs, mock_flash_erases;
extern unsigned mock_flash_begins;
extern int mock_flash_fail_after;
extern void (*mock_flash_write_check)(void);
void mock_flash_init(void);
void mock_flash_seed_legacy(void);
void mock_flash_flip(uint32_t address, uint16_t bits);
#endif
