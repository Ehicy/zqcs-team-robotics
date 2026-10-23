#include "board_link.h"
#include "calibration_flash.h"
#include "fallback.h"
#include <string.h>
#define BASE 0x0800F800UL
#define PAGE 1024U
#define SIZE 64U
static void pack(uint8_t b[SIZE], const fb_config_t *c, uint32_t seq)
{
    unsigned i;
    memset(b, 255, SIZE);
    link_put_u16(b, 0xF319);
    link_put_u16(b + 2, 1);
    link_put_u32(b + 4, seq);
    link_put_u16(b + 8, c->max_pwm);
    link_put_u16(b + 10, c->turn_percent);
    link_put_u16(b + 12, c->motor_negative);
    link_put_u16(b + 14, c->jog_us);
    for (i = 0; i < 4; i++)
        link_put_u16(b + 16 + 2 * i, c->gain[i]);
    for (i = 0; i < 3; i++)
    {
        link_put_u16(b + 24 + 6 * i, c->servo_min[i]);
        link_put_u16(b + 26 + 6 * i, c->servo_max[i]);
        link_put_u16(b + 28 + 6 * i, c->servo_reference[i]);
    }
    link_put_u16(b + 42, c->ramp_ms);
    link_put_u16(b + 44, c->brake_pwm);
    link_put_u16(b + 46, c->brake_ms);
    link_put_u16(b + 60, link_crc16(b, 60));
    link_put_u16(b + 62, 0xA55A);
}
static void read_at(uint32_t a, uint8_t b[SIZE])
{
    unsigned i;
    for (i = 0; i < SIZE; i += 2)
        link_put_u16(b + i, calibration_flash_read(a + i));
}
static int unpack(const uint8_t b[SIZE], fb_config_t *c)
{
    unsigned i;
    if (link_get_u16(b) != 0xF319 || link_get_u16(b + 2) != 1 || link_get_u16(b + 62) != 0xA55A ||
        link_get_u16(b + 60) != link_crc16(b, 60))
        return 0;
    c->max_pwm = link_get_u16(b + 8);
    c->turn_percent = link_get_u16(b + 10);
    c->motor_negative = link_get_u16(b + 12);
    c->jog_us = link_get_u16(b + 14);
    for (i = 0; i < 4; i++)
        c->gain[i] = link_get_u16(b + 16 + 2 * i);
    for (i = 0; i < 3; i++)
    {
        c->servo_min[i] = link_get_u16(b + 24 + 6 * i);
        c->servo_max[i] = link_get_u16(b + 26 + 6 * i);
        c->servo_reference[i] = link_get_u16(b + 28 + 6 * i);
    }
    c->ramp_ms = link_get_u16(b + 42);
    c->brake_pwm = link_get_u16(b + 44);
    c->brake_ms = link_get_u16(b + 46);
    return fb_config_valid(c);
}
static uint32_t latest(fb_config_t *c, uint32_t *seq)
{
    uint32_t a, found = 0;
    uint8_t b[SIZE];
    fb_config_t candidate;
    for (a = BASE; a < BASE + 2 * PAGE; a += SIZE)
    {
        read_at(a, b);
        if (unpack(b, &candidate) && (!found || link_sequence_newer(link_get_u32(b + 4), *seq)))
        {
            found = a;
            *seq = link_get_u32(b + 4);
            *c = candidate;
        }
    }
    return found;
}
int fb_store_load(fb_config_t *c)
{
    uint32_t seq = 0;
    return c && latest(c, &seq) != 0;
}
int fb_store_save(const fb_config_t *c)
{
    fb_config_t old;
    uint32_t seq = 0, a, page, previous;
    uint8_t b[SIZE], check[SIZE];
    unsigned i;
    int empty;
    if (!fb_config_valid(c))
        return 0;
    previous = latest(&old, &seq);
    pack(b, c, seq);
    if (previous)
    {
        pack(check, &old, seq);
        if (!memcmp(b, check, SIZE))
            return 2;
    }
    page = previous ? previous - (previous - BASE) % PAGE : BASE;
    for (a = page; a < page + PAGE; a += SIZE)
    {
        read_at(a, check);
        empty = 1;
        for (i = 0; i < SIZE; i++)
            if (check[i] != 255)
                empty = 0;
        if (empty)
            break;
    }
    calibration_flash_begin();
    if (a == page + PAGE)
    {
        page = page == BASE ? BASE + PAGE : BASE;
        if (!calibration_flash_erase(page))
            goto failed;
        a = page;
    }
    pack(b, c, previous ? seq + 1 : 0);
    for (i = 0; i < SIZE; i += 2)
        if (link_get_u16(b + i) != 65535 && !calibration_flash_program(a + i, link_get_u16(b + i)))
            goto failed;
    calibration_flash_end();
    read_at(a, check);
    return unpack(check, &old) && !memcmp(check, b, SIZE) ? 1 : 0;
failed:
    calibration_flash_end();
    return 0;
}
