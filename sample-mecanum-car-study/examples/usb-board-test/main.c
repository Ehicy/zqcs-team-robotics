/* Independent bench diagnostic. No motor/servo/PS2/IMU control. */
#include "stm32f1xx.h"
#include "tusb.h"
#include "board_config.h"
#if USB_TEST_SLAVE
#include "slave_protocol.h"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

uint32_t SystemCoreClock = 8000000U;
volatile uint32_t usb_test_ticks;
volatile uint32_t usb_test_commands;
volatile uint32_t usb_test_uart_rx_bytes;
volatile uint32_t usb_test_uart_overruns;
volatile uint32_t usb_test_output_drops;
volatile uint32_t usb_test_boot_stage;
static volatile uint8_t uart_rx[256];
static volatile uint16_t uart_rx_head, uart_rx_tail;
static uint8_t uart_tx[256];
static uint16_t uart_tx_head, uart_tx_tail;
static char usb_line[128], uart_line[128];
static uint16_t usb_line_len, uart_line_len;
static bool usb_line_overflow, uart_line_overflow;
static bool manual_led, led_on;
#if USB_TEST_SLAVE
volatile uint32_t usb_test_uart_requests;
volatile uint32_t usb_test_uart_bad_frames;
static bool reply_to_uart, uart_reply_overflow;
static char uart_reply[128];
static unsigned uart_reply_len;
#endif

/* Called before .data/.bss initialization by the existing GNU startup. */
void SystemInit(void)
{
    RCC->CR |= RCC_CR_HSION;
    RCC->CFGR = 0;
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON | RCC_CR_HSEON);
    RCC->CR |= RCC_CR_HSEON;
    uint32_t wait = 2000000U;
    while (!(RCC->CR & RCC_CR_HSERDY) && --wait) { }
    if (!wait) return;
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    /* 8 MHz HSE * 9; APB1=36MHz; USB=PLL/1.5=48MHz. */
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SCB->VTOR = FLASH_BASE;
}

static void led_write(bool on)
{
    led_on = on;
    GPIOC->BSRR = on ? (1U << (13 + 16)) : (1U << 13);
}

void SysTick_Handler(void) { ++usb_test_ticks; }
void USB_HP_CAN1_TX_IRQHandler(void) { tud_int_handler(0); }
void USB_LP_CAN1_RX0_IRQHandler(void) { tud_int_handler(0); }
void USBWakeUp_IRQHandler(void) { tud_int_handler(0); }

/* Blue Pill has a fixed external D+ pull-up; detach once at boot below. */
void dcd_connect(uint8_t rhport) { (void)rhport; }
void dcd_disconnect(uint8_t rhport) { (void)rhport; }

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t byte = (uint8_t)USART2->DR;
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE))
            ++usb_test_uart_overruns;
        else {
            uint16_t next = (uint16_t)((uart_rx_head + 1U) & 255U);
            if (next == uart_rx_tail) ++usb_test_uart_overruns;
            else {
                uart_rx[uart_rx_head] = byte;
                uart_rx_head = next;
                ++usb_test_uart_rx_bytes;
            }
        }
    }
}

static bool uart_enqueue(const char *text, unsigned length, bool newline)
{
    unsigned free = (uart_tx_tail - uart_tx_head - 1U) & 255U;
    if (length + (newline ? 2U : 0U) > free) return false;
    for (unsigned i = 0; i < length; ++i) {
        uart_tx[uart_tx_head] = (uint8_t)text[i];
        uart_tx_head = (uint16_t)((uart_tx_head + 1U) & 255U);
    }
    if (newline) {
        uart_tx[uart_tx_head] = '\r'; uart_tx_head = (uint16_t)((uart_tx_head + 1U) & 255U);
        uart_tx[uart_tx_head] = '\n'; uart_tx_head = (uint16_t)((uart_tx_head + 1U) & 255U);
    }
    return true;
}

static void usb_write(const char *text)
{
    uint32_t length = (uint32_t)strlen(text);
#if USB_TEST_SLAVE
    if (reply_to_uart) {
        if (length > sizeof(uart_reply) - uart_reply_len) uart_reply_overflow = true;
        else {
            memcpy(uart_reply + uart_reply_len, text, length);
            uart_reply_len += length;
        }
        return;
    }
#endif
    if (tud_cdc_write(text, length) != length) ++usb_test_output_drops;
}

static void usb_u32(uint32_t value, bool hex)
{
    char text[11];
    uint32_t base = hex ? 16U : 10U;
    unsigned count = 0;
    do {
        text[count++] = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value);
    if (hex) while (count < 8) text[count++] = '0';
    for (unsigned i = 0; i < count / 2; ++i) {
        char c = text[i]; text[i] = text[count - i - 1]; text[count - i - 1] = c;
    }
    text[count] = 0;
    usb_write(text);
}

static void info(void)
{
    usb_write("INFO firmware=" BOARD_TEST_FIRMWARE);
#if USB_TEST_SLAVE
    usb_write(" role=slave");
#endif
    usb_write(" uid=");
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    for (unsigned i = 0; i < 3; ++i) usb_u32(uid[i], true);
#if USB_TEST_SLAVE
    /* Keep UART INFO + RSP header within the original master's 127-byte limit. */
    if (!reply_to_uart) {
#endif
        usb_write(" uptime_ms="); usb_u32(usb_test_ticks, false);
        usb_write(" uart_rx="); usb_u32(usb_test_uart_rx_bytes, false);
        usb_write(" uart_errors="); usb_u32(usb_test_uart_overruns, false);
        usb_write(" usb_drops="); usb_u32(usb_test_output_drops, false);
#if USB_TEST_SLAVE
        usb_write(" uart_requests="); usb_u32(usb_test_uart_requests, false);
        usb_write(" bad_frames="); usb_u32(usb_test_uart_bad_frames, false);
    } else {
        usb_write(" uart_errors="); usb_u32(usb_test_uart_overruns, false);
    }
#endif
    usb_write(" led="); usb_write(led_on ? "1" : "0");
    usb_write("\r\n");
}

static void command(const char *line)
{
    ++usb_test_commands;
    if (!strcmp(line, "PING")) usb_write("PONG\r\n");
    else if (!strcmp(line, "INFO") || !strcmp(line, "ID")) info();
    else if (!strncmp(line, "ECHO ", 5)) {
        usb_write("ECHO "); usb_write(line + 5); usb_write("\r\n");
    } else if (!strcmp(line, "LED ON") || !strcmp(line, "LED OFF") ||
               !strcmp(line, "LED TOGGLE")) {
        manual_led = true;
        led_write(!strcmp(line, "LED ON") ? true :
                  !strcmp(line, "LED OFF") ? false : !led_on);
        usb_write(led_on ? "OK LED 1\r\n" : "OK LED 0\r\n");
    } else if (!strcmp(line, "LED AUTO")) {
        manual_led = false; usb_write("OK LED AUTO\r\n");
    } else if (!strncmp(line, "UART ", 5)) {
#if USB_TEST_SLAVE
        if (reply_to_uart) {
            usb_write("ERR COMMAND\r\n");
            return;
        }
#endif
        const char *payload = line + 5;
        unsigned length = (unsigned)strlen(payload);
        if (!uart_enqueue(payload, length, true)) usb_write("ERR UART BUSY\r\n");
        else usb_write("OK UART QUEUED\r\n");
    } else if (!strcmp(line, "HELP")) {
        usb_write("PING | INFO | ECHO text | LED ON/OFF/TOGGLE/AUTO | UART text\r\n");
    } else usb_write("ERR COMMAND\r\n");
}

#if USB_TEST_SLAVE
static void slave_request(const char *line)
{
    uint32_t sequence;
    const char *payload;
    if (!slave_parse_request(line, &sequence, &payload)) {
        ++usb_test_uart_bad_frames;
        return;
    }
    ++usb_test_uart_requests;
    uart_reply_len = 0;
    uart_reply_overflow = false;
    reply_to_uart = true;
    usb_write("RSP "); usb_u32(sequence, true); usb_write(" ");
    command(payload);
    reply_to_uart = false;
    if (uart_reply_overflow || !uart_enqueue(uart_reply, uart_reply_len, false))
        ++usb_test_uart_overruns;
}
#endif

static void usb_poll(void)
{
    if (!tud_cdc_connected()) {
        usb_line_len = 0; usb_line_overflow = false;
        return;
    }
    for (unsigned budget = 0; budget < 64 && tud_cdc_available(); ++budget) {
        char c = (char)tud_cdc_read_char();
        if (c == '\r') continue;
        if (c == '\n') {
            if (usb_line_overflow) usb_write("ERR LINE TOO LONG\r\n");
            else if (usb_line_len) {
                usb_line[usb_line_len] = 0;
                command(usb_line);
            }
            usb_line_len = 0; usb_line_overflow = false;
        } else if (usb_line_len < sizeof(usb_line) - 1U && c != 0)
            usb_line[usb_line_len++] = c;
        else usb_line_overflow = true;
    }
}

static void uart_poll(void)
{
    if (uart_tx_head != uart_tx_tail && (USART2->SR & USART_SR_TXE)) {
        USART2->DR = uart_tx[uart_tx_tail];
        uart_tx_tail = (uint16_t)((uart_tx_tail + 1U) & 255U);
    }
    for (unsigned budget = 0; budget < 64 && uart_rx_head != uart_rx_tail; ++budget) {
        char c = (char)uart_rx[uart_rx_tail];
        uart_rx_tail = (uint16_t)((uart_rx_tail + 1U) & 255U);
        if (c == '\r') continue;
        if (c == '\n') {
#if USB_TEST_SLAVE
            if (uart_line_overflow) ++usb_test_uart_bad_frames;
            else if (uart_line_len) {
                uart_line[uart_line_len] = 0;
                slave_request(uart_line);
            }
#else
            if (tud_cdc_connected()) {
                if (uart_line_overflow) usb_write("ERR UART LINE TOO LONG\r\n");
                else if (uart_line_len) {
                    uart_line[uart_line_len] = 0;
                    usb_write("UART_RX "); usb_write(uart_line); usb_write("\r\n");
                }
            }
#endif
            uart_line_len = 0; uart_line_overflow = false;
        } else if (uart_line_len < sizeof(uart_line) - 1U && c != 0)
            uart_line[uart_line_len++] = c;
        else uart_line_overflow = true;
    }
}

int main(void)
{
    usb_test_boot_stage = 1;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;
    GPIOC->BSRR = 1U << 13;
    GPIOC->CRH = (GPIOC->CRH & ~(15U << 20)) | (2U << 20);
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        /* HSE failed: no inaccurate-clock USB initialization. */
        usb_test_boot_stage = 0xE1;
        SysTick_Config(8000U);
        while (1) led_write(((usb_test_ticks / 100U) & 1U) == 0);
    }
    SystemCoreClock = 72000000U;
    SysTick_Config(SystemCoreClock / 1000U);
    /* USART2 PA2 TX, PA3 RX; input pull-up suppresses noise while unconnected. */
    GPIOA->CRL = (GPIOA->CRL & ~((15U << 8) | (15U << 12))) | (11U << 8) | (8U << 12);
    GPIOA->BSRR = 1U << 3;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    USART2->BRR = 313U;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);
    /* Briefly pull D+ low to force Windows to enumerate after an SWD reset. */
    GPIOA->BRR = 1U << 12;
    GPIOA->CRH = (GPIOA->CRH & ~(15U << 16)) | (2U << 16);
    uint32_t begin = usb_test_ticks;
    while (usb_test_ticks - begin < 25U) { }
    GPIOA->CRH = (GPIOA->CRH & ~((15U << 12) | (15U << 16))) | (11U << 12) | (11U << 16);
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    NVIC_SetPriority(USB_HP_CAN1_TX_IRQn, 2);
    NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 2);
    NVIC_SetPriority(USBWakeUp_IRQn, 2);
    usb_test_boot_stage = 2;
    tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(0, &init)) {
        usb_test_boot_stage = 0xE2;
        while (1) { }
    }
    usb_test_boot_stage = 3;
    uint32_t last_blink = usb_test_ticks;
    while (1) {
        tud_task();
        usb_poll();
        uart_poll();
        tud_cdc_write_flush();
        if (!manual_led && usb_test_ticks - last_blink >= (tud_mounted() ? 1000U : 250U)) {
            last_blink = usb_test_ticks;
            led_write(!led_on);
        }
    }
}
