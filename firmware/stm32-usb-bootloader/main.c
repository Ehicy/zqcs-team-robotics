#include "common.h"
#include "dfu.h"

int main(void) {
    systemReset();
    boot_safe_early();
    setupCLK(); setupLEDAndButton(); setupFLASH(); setupUSB();
    int flag=checkAndClearBootloaderFlag();
    bool stay=flag==1 || readButtonState() || !checkUserCode(USER_CODE_FLASH0X8002000);
    /* PB2 high wins even immediately after a successful upload. */
    for(unsigned i=0; stay || i<6; ++i) {
        strobePin(LED_BANK,LED_PIN,1,BLINK_SLOW,LED_ON_STATE);
        if(dfuUploadStarted()) dfuFinishUpload();
    }
    __asm volatile("cpsid i" ::: "memory");
    if(!dfuUploadStarted() && checkUserCode(USER_CODE_FLASH0X8002000))
        jumpToUser(USER_CODE_FLASH0X8002000);
    __asm volatile("cpsie i" ::: "memory");
    dfuFinishUpload();
}
