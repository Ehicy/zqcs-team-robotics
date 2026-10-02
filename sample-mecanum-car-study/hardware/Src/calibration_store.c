#include "calibration_store.h"
#include "calibration_flash.h"
#define RECORD_BYTES 16U
#define RECORD_WORDS 8U
#define RECORD_MAGIC 0xC217U
#define RECORD_COMMIT 0xA55AU

typedef struct
{
    uint8_t valid;
    uint32_t address;
    uint32_t sequence;
    strafe_calibration_t data;
} latest_record_t;

static uint8_t gains_valid(const strafe_calibration_t *data)
{
    return data != 0 && data->front_percent >= CAL_GAIN_MIN &&
           data->front_percent <= CAL_GAIN_MAX && data->rear_percent >= CAL_GAIN_MIN &&
           data->rear_percent <= CAL_GAIN_MAX;
}
static uint16_t record_crc(const uint16_t *words)
{
    uint16_t crc = 0xFFFFU;
    uint8_t byte, bit;
    for (byte = 0; byte < 12U; byte++)
    {
        crc ^= (uint16_t)((words[byte / 2U] >> ((byte & 1U) * 8U)) & 0xFFU) << 8;
        for (bit = 0; bit < 8U; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
    }
    return crc;
}
static void read_record(uint32_t address, uint16_t *words)
{
    uint8_t i;
    for (i = 0; i < RECORD_WORDS; i++) words[i] = calibration_flash_read(address + 2U * i);
}
static uint8_t record_valid(const uint16_t *words, strafe_calibration_t *data)
{
    data->front_percent = (uint8_t)words[4];
    data->rear_percent = (uint8_t)(words[4] >> 8);
    return words[0] == RECORD_MAGIC && words[1] == CAL_STORE_VERSION &&
           words[7] == RECORD_COMMIT && (uint16_t)(words[4] ^ words[5]) == 0xFFFFU &&
           words[6] == record_crc(words) && gains_valid(data);
}
static latest_record_t latest_record(void)
{
    latest_record_t latest = {0, 0, 0, {0, 0}};
    uint32_t offset;
    uint16_t words[RECORD_WORDS];
    strafe_calibration_t data;
    for (offset = 0; offset < CAL_STORE_BYTES; offset += RECORD_BYTES)
    {
        uint32_t sequence, difference;
        read_record(CAL_STORE_BASE + offset, words);
        if (!record_valid(words, &data)) continue;
        sequence = (uint32_t)words[2] | ((uint32_t)words[3] << 16);
        difference = sequence - latest.sequence;
        if (!latest.valid || (difference != 0U && difference < 0x80000000UL))
        {
            latest.valid = 1;
            latest.address = CAL_STORE_BASE + offset;
            latest.sequence = sequence;
            latest.data = data;
        }
    }
    return latest;
}
static uint32_t empty_slot(uint32_t page)
{
    uint32_t offset;
    uint8_t word;
    for (offset = 0; offset < CAL_STORE_PAGE_BYTES; offset += RECORD_BYTES)
    {
        for (word = 0; word < RECORD_WORDS; word++)
            if (calibration_flash_read(page + offset + 2U * word) != 0xFFFFU) break;
        if (word == RECORD_WORDS) return page + offset;
    }
    return 0;
}
uint8_t calibration_store_load(strafe_calibration_t *data)
{
    latest_record_t latest;
    if (data == 0) return 0;
    latest = latest_record();
    if (!latest.valid) return 0;
    *data = latest.data;
    return 1;
}
uint8_t calibration_store_save(const strafe_calibration_t *data)
{
    latest_record_t latest;
    uint32_t page, address, sequence;
    uint16_t words[RECORD_WORDS], verify[RECORD_WORDS];
    strafe_calibration_t checked;
    uint8_t word;
    if (!gains_valid(data)) return CAL_STORE_ERROR;
    latest = latest_record();
    if (latest.valid && latest.data.front_percent == data->front_percent &&
        latest.data.rear_percent == data->rear_percent) return CAL_STORE_UNCHANGED;
    page = latest.valid ? latest.address - (latest.address - CAL_STORE_BASE) % CAL_STORE_PAGE_BYTES :
                          CAL_STORE_BASE;
    address = empty_slot(page);
    calibration_flash_begin();
    if (address == 0)
    {
        if (latest.valid) page = page == CAL_STORE_BASE ? CAL_STORE_BASE + CAL_STORE_PAGE_BYTES : CAL_STORE_BASE;
        if (!calibration_flash_erase(page)) goto failed;
        address = page;
    }
    sequence = latest.valid ? latest.sequence + 1U : 0U;
    words[0] = RECORD_MAGIC; words[1] = CAL_STORE_VERSION;
    words[2] = (uint16_t)sequence; words[3] = (uint16_t)(sequence >> 16);
    words[4] = (uint16_t)data->front_percent | ((uint16_t)data->rear_percent << 8);
    words[5] = (uint16_t)~words[4]; words[6] = record_crc(words); words[7] = RECORD_COMMIT;
    /* 提交标记最后写；旧有效记录在新提交成功前始终保留。 */
    for (word = 0; word < RECORD_WORDS; word++)
        if (words[word] != 0xFFFFU &&
            !calibration_flash_program(address + 2U * word, words[word])) goto failed;
    calibration_flash_end();
    read_record(address, verify);
    if (!record_valid(verify, &checked)) return CAL_STORE_ERROR;
    return CAL_STORE_SAVED;
failed:
    calibration_flash_end();
    return CAL_STORE_ERROR;
}
