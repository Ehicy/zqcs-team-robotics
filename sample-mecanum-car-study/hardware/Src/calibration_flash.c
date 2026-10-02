#include "calibration_flash.h"
#include "calibration_store.h"
#include "stm32f10x_flash.h"

uint16_t calibration_flash_read(uint32_t address)
{
    return *(volatile const uint16_t *)(uintptr_t)address;
}
void calibration_flash_begin(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
}
void calibration_flash_end(void) { FLASH_Lock(); }
uint8_t calibration_flash_erase(uint32_t page)
{
    if (page != CAL_STORE_BASE && page != CAL_STORE_BASE + CAL_STORE_PAGE_BYTES) return 0;
    return FLASH_ErasePage(page) == FLASH_COMPLETE;
}
uint8_t calibration_flash_program(uint32_t address, uint16_t value)
{
    if (address < CAL_STORE_BASE || address >= CAL_STORE_BASE + CAL_STORE_BYTES ||
        (address & 1U) != 0U) return 0;
    return FLASH_ProgramHalfWord(address, value) == FLASH_COMPLETE &&
           calibration_flash_read(address) == value;
}
