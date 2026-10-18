/* Host-only replacement of MMIO headers for the production DFU state machine. */
#define __COMMON_H
#define __HARDWARE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define TRUE true
#define FALSE false
#define RTC_BOOTLOADER_JUST_UPLOADED 0x424D
#define GET_REG(addr) test_read(addr)
typedef struct {
    u16 Usb_wOffset, Usb_wLength, Usb_rLength;
} fake_ctrl_t;
typedef struct {
    u8 USBbRequest, USBbmRequestType, Current_Configuration, Current_AlternateSetting;
    u16 USBwLength, USBwValue, USBwIndex;
    fake_ctrl_t Ctrl_Info;
} fake_info_t;
extern fake_info_t *pInformation;
uint32_t test_read(uint32_t a);
bool flashErasePage(u32 a);
bool flashWriteWord(u32 a,u32 w);
void flashLock(void);
void flashUnlock(void);
void setupFLASH(void);
void boot_feed(void);
void bkp10Write(u16 value);
void systemHardReset(void);
