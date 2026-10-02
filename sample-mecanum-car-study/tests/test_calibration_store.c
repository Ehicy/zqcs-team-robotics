#include <assert.h>
#include <stdio.h>
#include "calibration_store.h"
#include "mock_calibration_flash.h"
static void loaded(uint8_t front, uint8_t rear)
{
    strafe_calibration_t data;
    assert(calibration_store_load(&data));
    assert(data.front_percent == front && data.rear_percent == rear);
}
static void fill_page(void)
{
    unsigned i;
    strafe_calibration_t data = {85,100};
    mock_flash_init();
    for (i = 0; i < 64; i++)
    {
        data.front_percent = i % 2 ? 80 : 85;
        assert(calibration_store_save(&data) == CAL_STORE_SAVED);
    }
    loaded(80,100);
    assert(mock_flash_erases == 0);
}
int main(void)
{
    unsigned programs, erases, i;
    strafe_calibration_t data = {85,100}, invalid = {49,100};
    mock_flash_init();
    assert(!calibration_store_load(&data) && !calibration_store_load(0));
    assert(calibration_store_save(0) == CAL_STORE_ERROR);
    assert(calibration_store_save(&invalid) == CAL_STORE_ERROR);
    assert(calibration_store_save(&data) == CAL_STORE_SAVED); loaded(85,100);
    programs = mock_flash_programs; erases = mock_flash_erases;
    assert(calibration_store_save(&data) == CAL_STORE_UNCHANGED);
    assert(mock_flash_programs == programs && mock_flash_erases == erases);
    data.front_percent = 75;
    assert(calibration_store_save(&data) == CAL_STORE_SAVED); loaded(75,100);
    mock_flash_flip(CAL_STORE_BASE + 16U + 8U, 1U); loaded(85,100); /* CRC/互补拒绝损坏 */

    /* 每个半字写入点断电：缺少最后提交字的新记录都不能覆盖旧值。 */
    for (i = 0; i < 8; i++)
    {
        mock_flash_init(); data.front_percent = 85;
        assert(calibration_store_save(&data) == CAL_STORE_SAVED);
        data.front_percent = 70; mock_flash_fail_after = (int)i;
        assert(calibration_store_save(&data) == CAL_STORE_ERROR); loaded(85,100);
        mock_flash_fail_after = -1;
        assert(calibration_store_save(&data) == CAL_STORE_SAVED); loaded(70,100);
    }
    /* 切页时在擦除及所有写入点中断，旧页记录仍可恢复。 */
    for (i = 0; i < 9; i++)
    {
        fill_page(); data.front_percent = 70; mock_flash_fail_after = (int)i;
        assert(calibration_store_save(&data) == CAL_STORE_ERROR); loaded(80,100);
        mock_flash_fail_after = -1;
        assert(calibration_store_save(&data) == CAL_STORE_SAVED); loaded(70,100);
    }
    fill_page();
    for (i = 0; i < 160; i++)
    {
        data.front_percent = i % 2 ? 60 : 65;
        assert(calibration_store_save(&data) == CAL_STORE_SAVED);
        loaded(data.front_percent,100);
    }
    assert(mock_flash_erases == 3);
    puts("PASS: calibration flash bounds, CRC, no duplicate write, append journal, page rotation, interrupted write/erase recovery");
    return 0;
}
