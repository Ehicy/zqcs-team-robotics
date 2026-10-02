#include <assert.h>
#include <string.h>
#include "calibration_flash.h"
#include "calibration_store.h"
#include "mock_calibration_flash.h"
static uint16_t flash[CAL_STORE_BYTES / 2];
static uint8_t unlocked;
unsigned mock_flash_programs, mock_flash_erases;
unsigned mock_flash_begins;
int mock_flash_fail_after = -1;
void (*mock_flash_write_check)(void);
void mock_flash_init(void)
{
    memset(flash, 0xFF, sizeof flash);
    unlocked = 0; mock_flash_programs = mock_flash_erases = mock_flash_begins = 0;
    mock_flash_fail_after = -1; mock_flash_write_check = 0;
}
void mock_flash_seed_legacy(void)
{
    /* 2026-10-02实板读回的v1序号0、85/100记录，CRC独立核验为F7D9。 */
    static const uint16_t legacy[8] = {0xC217,1,0,0,0x6455,0x9BAA,0xF7D9,0xA55A};
    mock_flash_init();
    memcpy(flash, legacy, sizeof legacy);
}
static unsigned index_of(uint32_t address)
{
    assert(address >= CAL_STORE_BASE && address < CAL_STORE_BASE + CAL_STORE_BYTES);
    assert((address & 1U) == 0);
    return (address - CAL_STORE_BASE) / 2;
}
uint16_t calibration_flash_read(uint32_t address) { return flash[index_of(address)]; }
void mock_flash_flip(uint32_t address, uint16_t bits) { flash[index_of(address)] ^= bits; }
void calibration_flash_begin(void)
{
    assert(!unlocked); unlocked = 1; mock_flash_begins++;
    if (mock_flash_write_check) mock_flash_write_check();
}
void calibration_flash_end(void) { unlocked = 0; }
static uint8_t operation_allowed(void)
{
    assert(unlocked);
    if (mock_flash_write_check) mock_flash_write_check();
    if (mock_flash_fail_after == 0) return 0;
    if (mock_flash_fail_after > 0) mock_flash_fail_after--;
    return 1;
}
uint8_t calibration_flash_program(uint32_t address, uint16_t value)
{
    unsigned index = index_of(address);
    if (!operation_allowed()) return 0;
    assert(flash[index] == 0xFFFFU);
    flash[index] = value; mock_flash_programs++;
    return 1;
}
uint8_t calibration_flash_erase(uint32_t page)
{
    unsigned index = index_of(page);
    assert(page == CAL_STORE_BASE || page == CAL_STORE_BASE + CAL_STORE_PAGE_BYTES);
    if (!operation_allowed()) return 0;
    memset(&flash[index], 0xFF, CAL_STORE_PAGE_BYTES); mock_flash_erases++;
    return 1;
}
