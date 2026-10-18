/* *****************************************************************************
 * The MIT License
 *
 * Copyright (c) 2010 LeafLabs LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * ****************************************************************************/

/**
 *  @file hardware.c
 *
 *  @brief init routines to setup clocks, interrupts, also destructor functions.
 *  does not include USB stuff. EEPROM read/write functions.
 *
 */
#include "common.h"
#include "hardware.h"
/*
void setPin(u32 bank, u8 pin) {
    u32 pinMask = 0x1 << (pin);
    SET_REG(GPIO_BSRR(bank), pinMask);
}

void resetPin(u32 bank, u8 pin) {
    u32 pinMask = 0x1 << (16 + pin);
    SET_REG(GPIO_BSRR(bank), pinMask);
}
*/
void gpio_write_bit(u32 bank, u8 pin, u8 val) {
    val = !val;          // "set" bits are lower than "reset" bits
    SET_REG(GPIO_BSRR(bank), (1U << pin) << (16 * val));
}

bool readPin(u32 bank, u8 pin) {
    // todo, implement read
    if (GET_REG(GPIO_IDR(bank)) & (0x01 << pin)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

bool readButtonState() {
    // todo, implement read
    bool state=FALSE;
#if defined(BUTTON_BANK) && defined (BUTTON_PIN) && defined (BUTTON_PRESSED_STATE)
    if (GET_REG(GPIO_IDR(BUTTON_BANK)) & (0x01 << BUTTON_PIN))
    {
        state = TRUE;
    }

    if (BUTTON_PRESSED_STATE==0)
    {
        state=!state;
    }
#endif
    return state;
}

void strobePin(u32 bank, u8 pin, u8 count, u32 rate,u8 onState)
{
    gpio_write_bit( bank,pin,1-onState);

    u32 c;
    while (count-- > 0)
    {
        for (c = rate; c > 0; c--)
        {
            boot_feed(); asm volatile("nop");
        }

        gpio_write_bit( bank,pin,onState);

        for (c = rate; c > 0; c--)
        {
            boot_feed(); asm volatile("nop");
        }
        gpio_write_bit( bank,pin,1-onState);
    }
}

void systemReset(void) {
    SET_REG(RCC_CR, GET_REG(RCC_CR)     | 0x00000001);
    SET_REG(RCC_CFGR, GET_REG(RCC_CFGR) & 0xF8FF0000);
    SET_REG(RCC_CR, GET_REG(RCC_CR)     & 0xFEF6FFFF);
    SET_REG(RCC_CR, GET_REG(RCC_CR)     & 0xFFFBFFFF);
    SET_REG(RCC_CFGR, GET_REG(RCC_CFGR) & 0xFF80FFFF);

    SET_REG(RCC_CIR, 0x00000000);  /* disable all RCC interrupts */
}

void setupCLK(void) {
    /* Target: Blue Pill, 8 MHz crystal, 72 MHz SYSCLK / 48 MHz USB. */
    SET_REG(RCC_CR, GET_REG(RCC_CR) | 0x00010001);
    unsigned timeout=2000000;
    while(!(GET_REG(RCC_CR)&0x00020000)) {
        boot_feed(); if(!--timeout) systemHardReset();
    }
    SET_REG(FLASH_ACR,0x12);
    SET_REG(RCC_CFGR,0x001D0400);
    SET_REG(RCC_CR,GET_REG(RCC_CR)|0x01000000);
    timeout=2000000;
    while(!(GET_REG(RCC_CR)&0x02000000)) {
        boot_feed(); if(!--timeout) systemHardReset();
    }
    SET_REG(RCC_CFGR,GET_REG(RCC_CFGR)|2);
    timeout=2000000;
    while((GET_REG(RCC_CFGR)&0x0C)!=0x08) {
        boot_feed(); if(!--timeout) systemHardReset();
    }
    pRCC->APB2ENR |= 0x1C; /* GPIO A/B/C only; SWD left enabled. */
    pRCC->APB1ENR |= RCC_APB1ENR_USB_CLK;
}



void setupLEDAndButton (void) {
    // SET_REG(AFIO_MAPR,(GET_REG(AFIO_MAPR) & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_NO_JTAG_NO_SW);// Try to disable SWD AND JTAG so we can use those pins (not sure if this works).

#if defined(BUTTON_BANK) && defined (BUTTON_PIN) && defined (BUTTON_PRESSED_STATE)
    SET_REG(GPIO_CR(BUTTON_BANK,BUTTON_PIN),(GET_REG(GPIO_CR(BUTTON_BANK,BUTTON_PIN)) & crMask(BUTTON_PIN)) | BUTTON_INPUT_MODE << CR_SHITF(BUTTON_PIN));

    gpio_write_bit(BUTTON_BANK, BUTTON_PIN,1-BUTTON_PRESSED_STATE);// set pulldown resistor in case there is no button.
#endif
#if defined(LED_BANK) && defined(LED_PIN) && defined(LED_ON_STATE)
    SET_REG(GPIO_CR(LED_BANK,LED_PIN),(GET_REG(GPIO_CR(LED_BANK,LED_PIN)) & crMask(LED_PIN)) | CR_OUTPUT_PP << CR_SHITF(LED_PIN));
#endif
}

void setupFLASH() {
    /* configure the HSI oscillator */
    if ((pRCC->CR & 0x01) == 0x00) {
        u32 rwmVal = pRCC->CR;
        rwmVal |= 0x01;
        pRCC->CR = rwmVal;
    }

    /* wait for it to come on */
    while ((pRCC->CR & 0x02) == 0x00) {}
}

bool checkUserCode(u32 usrAddr) {
    return usrAddr==UPDATE_BASE && update_vectors_valid(GET_REG(usrAddr),GET_REG(usrAddr+4),UPDATE_CAPACITY);
}


__attribute__((naked,noreturn)) void setMspAndJump(u32 usrAddr __attribute__((unused))) {
    __asm volatile(
        "ldr r1, [r0]\n"
        "ldr r2, [r0, #4]\n"
        "ldr r3, =0xE000ED08\n"
        "str r0, [r3]\n"
        "movs r3, #0\n"
        "msr control, r3\n"
        "msr basepri, r3\n"
        "msr faultmask, r3\n"
        "msr msp, r1\n"
        "dsb\n"
        "isb\n"
        "cpsie i\n"
        "bx r2\n");
}



void jumpToUser(u32 usrAddr) {
    __asm volatile("cpsid i" ::: "memory");
    flashLock(); nvicDisableInterrupts(); usbDsbBus();
    SET_REG(SCB_BASE+4,(1u<<25)|(1u<<27)); /* clear pending SysTick/PendSV */
    SET_REG(STK+4,0); SET_REG(STK+8,0);
    /* USB held reset until APP chooses to initialize it. STBY remains LOW. */
    pRCC->APB1RSTR |= RCC_APB1ENR_USB_CLK;
    pRCC->APB1ENR &= ~RCC_APB1ENR_USB_CLK;
    systemReset(); boot_feed();
    setMspAndJump(usrAddr);
}


void bkp10Write(u16 value)
{
        // Enable clocks for the backup domain registers
        pRCC->APB1ENR |= (RCC_APB1ENR_PWR_CLK | RCC_APB1ENR_BKP_CLK);

        // Disable backup register write protection
        pPWR->CR |= PWR_CR_DBP;

        // store value in pBK DR10
        pBKP->DR10 = value;

        // Re-enable backup register write protection
        pPWR->CR &=~ PWR_CR_DBP;
}

int checkAndClearBootloaderFlag()
{
    u8 flagSet = 0x00; /* tri-state 0/1/2; never use C99 _Bool here */

    // Enable clocks for the backup domain registers
    pRCC->APB1ENR |= (RCC_APB1ENR_PWR_CLK | RCC_APB1ENR_BKP_CLK);

    switch (pBKP->DR10)
    {
        case RTC_BOOTLOADER_FLAG:
            flagSet = 0x01;
            break;
        case RTC_BOOTLOADER_JUST_UPLOADED:
            flagSet = 0x02;
            break;
    }

    if (flagSet!=0x00)
    {
        bkp10Write(0x0000);// Clear the flag
        // Disable clocks
        pRCC->APB1ENR &= ~(RCC_APB1ENR_PWR_CLK | RCC_APB1ENR_BKP_CLK);
    }



    return flagSet;
}



void nvicInit(NVIC_InitTypeDef *NVIC_InitStruct) {
    u32 tmppriority = 0x00;
    u32 tmpreg      = 0x00;
    u32 tmpmask     = 0x00;
    u32 tmppre      = 0;
    u32 tmpsub      = 0x0F;

    SCB_TypeDef *rSCB = (SCB_TypeDef *) SCB_BASE;
    NVIC_TypeDef *rNVIC = (NVIC_TypeDef *) NVIC_BASE;


    /* Compute the Corresponding IRQ Priority --------------------------------*/
    tmppriority = (0x700 - (rSCB->AIRCR & (u32)0x700)) >> 0x08;
    tmppre = (0x4 - tmppriority);
    tmpsub = tmpsub >> tmppriority;

    tmppriority = (u32)NVIC_InitStruct->NVIC_IRQChannelPreemptionPriority << tmppre;
    tmppriority |=  NVIC_InitStruct->NVIC_IRQChannelSubPriority & tmpsub;

    tmppriority = tmppriority << 0x04;
    tmppriority = ((u32)tmppriority) << ((NVIC_InitStruct->NVIC_IRQChannel & (u8)0x03) * 0x08);

    tmpreg = rNVIC->IPR[(NVIC_InitStruct->NVIC_IRQChannel >> 0x02)];
    tmpmask = (u32)0xFF << ((NVIC_InitStruct->NVIC_IRQChannel & (u8)0x03) * 0x08);
    tmpreg &= ~tmpmask;
    tmppriority &= tmpmask;
    tmpreg |= tmppriority;

    rNVIC->IPR[(NVIC_InitStruct->NVIC_IRQChannel >> 0x02)] = tmpreg;

    /* Enable the Selected IRQ Channels --------------------------------------*/
    rNVIC->ISER[(NVIC_InitStruct->NVIC_IRQChannel >> 0x05)] =
        (u32)0x01 << (NVIC_InitStruct->NVIC_IRQChannel & (u8)0x1F);
}

void nvicDisableInterrupts() {
    NVIC_TypeDef *rNVIC = (NVIC_TypeDef *) NVIC_BASE;
    rNVIC->ICER[0] = 0xFFFFFFFF;
    rNVIC->ICER[1] = 0xFFFFFFFF;
    rNVIC->ICPR[0] = 0xFFFFFFFF;
    rNVIC->ICPR[1] = 0xFFFFFFFF;

    SET_REG(STK_CTRL, 0x04); /* disable the systick, which operates separately from nvic */
}

void systemHardReset(void) {
    SCB_TypeDef *rSCB = (SCB_TypeDef *) SCB_BASE;

    /* Reset  */
    rSCB->AIRCR = (u32)AIRCR_RESET_REQ;

    /*  should never get here */
    while (1) {
        boot_feed(); asm volatile("nop");
    }
}

bool flashErasePage(u32 pageAddr) {
    if(pageAddr<UPDATE_BASE || pageAddr>=UPDATE_END || pageAddr%UPDATE_PAGE) return FALSE;
    boot_feed();
    SET_REG(FLASH_SR,0x34); /* clear EOP/PGERR/WRPRTERR */
    SET_REG(FLASH_CR,FLASH_CR_PER);
    SET_REG(FLASH_AR,pageAddr);
    SET_REG(FLASH_CR,FLASH_CR_START|FLASH_CR_PER);
    unsigned timeout=2000000;
    while(GET_REG(FLASH_SR)&FLASH_SR_BSY) { boot_feed(); if(!--timeout) return FALSE; }
    u32 errors=GET_REG(FLASH_SR)&0x14;
    SET_REG(FLASH_CR,0);
    if(errors) return FALSE;
    for(u32 i=0;i<UPDATE_PAGE;i+=4) if(GET_REG(pageAddr+i)!=0xFFFFFFFF) return FALSE;
    return TRUE;
}

bool flashErasePages(u32 pageAddr, u16 n) {
    while (n-- > 0) {
        if (!flashErasePage(pageAddr + wTransferSize * n)) {
            return FALSE;
        }
    }

    return TRUE;
}

bool flashWriteWord(u32 addr,u32 word) {
    if(addr<UPDATE_BASE || addr>UPDATE_END-4 || (addr&3)) return FALSE;
    SET_REG(FLASH_SR,0x34);
    SET_REG(FLASH_CR,FLASH_CR_PG);
    /* High half first: incomplete MSP commit fails strict vector validation. */
    for(int half=1;half>=0;--half) {
        boot_feed();
        *((vu16 *)addr+half)=(u16)(word>>(16*half));
        unsigned timeout=2000000;
        while(GET_REG(FLASH_SR)&FLASH_SR_BSY) { boot_feed(); if(!--timeout) return FALSE; }
        if(GET_REG(FLASH_SR)&0x14) { SET_REG(FLASH_CR,0); return FALSE; }
    }
    SET_REG(FLASH_CR,0);
    return GET_REG(addr)==word;
}


void flashLock() {
    /* take down the HSI oscillator? it may be in use elsewhere */

    /* ensure all FPEC functions disabled and lock the FPEC */
    SET_REG(FLASH_CR, 0x00000080);
}

void flashUnlock() {
    /* unlock the flash */
    SET_REG(FLASH_KEYR, FLASH_KEY1);
    SET_REG(FLASH_KEYR, FLASH_KEY2);
}


// Used to create the control register masking pattern, when setting control register mode.
unsigned int crMask(int pin)
{
    unsigned int mask;
    if (pin>=8)
    {
        pin-=8;
    }
    mask = 0x0F << (pin<<2);
    return ~mask;
}

#define FLASH_SIZE_REG 0x1FFFF7E0
int getFlashEnd(void) { return UPDATE_END; }


int getFlashPageSize(void) { return UPDATE_PAGE; }


/* Called before .data/.bss initialization: no global state may be accessed. */
void boot_feed(void) { SET_REG(0x40003000,0xAAAA); }
void boot_safe_early(void) {
    pRCC->APB2ENR |= (1u<<3);
    SET_REG(GPIO_BSRR(GPIOB),1u<<31); /* PB15 ODR=0 BEFORE output mode */
    SET_REG(GPIO_CRH(GPIOB),(GET_REG(GPIO_CRH(GPIOB))&0x0FFFFFFF)|0x10000000);
    /* Extend an already-active HARDWARE IWDG only; do not start software IWDG. */
    boot_feed();
    if(!(GET_REG(FLASH_OBR)&4)) {
        SET_REG(0x40003000,0x5555);
        SET_REG(0x40003004,6); SET_REG(0x40003008,0xFFF);
        unsigned timeout=1000000;
        while(GET_REG(0x4000300C) && --timeout) boot_feed();
        boot_feed();
    }
}
