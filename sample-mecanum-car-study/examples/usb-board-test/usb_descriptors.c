/* Adapted from TinyUSB examples, Copyright (c) 2019 Ha Thach (tinyusb.org).
 * MIT License: https://github.com/hathach/tinyusb/blob/0.18.0/LICENSE
 * Full license is retained in THIRD_PARTY_LICENSES.txt beside this file.
 */
#include "stm32f1xx.h"
#include "tusb.h"
#include "board_config.h"
#include <string.h>

/* Upstream example identifiers, for local laboratory tests only. */
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE,
    .idProduct = 0x4001,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&device;
}

static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0,
                          TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t descriptor[40];
    static const char *const names[] = {
        "", "ZQCS Lab", BOARD_TEST_PRODUCT, "", "USB diagnostics"
    };
    static const char hex[] = "0123456789ABCDEF";
    uint8_t count = 0;
    (void)langid;
    if (index == 0) {
        descriptor[1] = 0x0409;
        count = 1;
    } else if (index == 3) {
        const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
        for (uint8_t word = 0; word < 3; ++word) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                descriptor[++count] = hex[(uid[word] >> shift) & 15U];
            }
        }
    } else {
        if (index >= sizeof(names) / sizeof(names[0])) return NULL;
        count = (uint8_t)strlen(names[index]);
        for (uint8_t i = 0; i < count; ++i)
            descriptor[i + 1] = (uint8_t)names[index][i];
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U * count + 2U));
    return descriptor;
}
