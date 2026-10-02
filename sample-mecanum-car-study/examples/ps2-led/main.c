#include "stm32f10x.h"
#include "delay.h"
#include "ps2.h"

/* 常见 F103C8 核心板用户 LED：PC13，低电平亮。POWER 灯不可编程。
 * 沿用仓库接线：DAT=PB14, CMD=PB15, CS=PB12, CLK=PB13。
 * 此入口只初始化手柄和 LED，供无电机/舵机的独立检查使用。
 */
#define LED_PIN GPIO_Pin_13
#define CIRCLE_MASK (1U << (PS2_BUTTON_CIRCLE - 8U)) /* btn2 的 bit5 */
#define PRESSURE_FRAME_SIZE 21U

/* 在调试器 Watch 中查看；volatile 保证每次状态更新写入 SRAM。 */
typedef struct
{
    uint32_t magic;           /* 0x5053324C：PS2 LED test */
    uint32_t loops;
    uint32_t valid_frames;
    uint32_t invalid_frames;
    uint32_t circle_presses;
    uint32_t led_on;
    uint32_t frame_valid;
    uint32_t circle_down;
    uint32_t mode;
    uint8_t raw[PRESSURE_FRAME_SIZE];
} ps2_led_debug_t;

volatile ps2_led_debug_t ps2_led_debug;

static void led_set(uint8_t on)
{
    if (on != 0U)
        GPIO_ResetBits(GPIOC, LED_PIN);
    else
        GPIO_SetBits(GPIOC, LED_PIN);
    ps2_led_debug.led_on = on;
}

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_SetBits(GPIOC, LED_PIN);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);
}

/* 复用仓库的 GPIO 初始化和逐位通信，另存原始帧用于排查接线。
 * 原小车程序只接受 0x73；本测试也识别 0x41 数字模式和 0x79 压感
 * 模式的圆圈键。0x79 读满 21 字节，其余读 9 字节。
 */
static uint8_t read_circle_frame(void)
{
    uint8_t i;
    uint8_t length = PS2_FRAME_SIZE;
    uint8_t mode;
    for (i = 0; i < PRESSURE_FRAME_SIZE; i++)
        ps2_led_debug.raw[i] = 0;
    GPIO_SetBits(GPIOB, GPIO_Pin_15 | GPIO_Pin_13);
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    delay_us(16);
    for (i = 0; i < length; i++)
    {
        uint8_t command = (i == 0U) ? 0x01U : ((i == 1U) ? 0x42U : 0x00U);
        ps2_led_debug.raw[i] = ps2_comm(command);
        if (i == 1U && ps2_led_debug.raw[1] == 0x79U)
            length = PRESSURE_FRAME_SIZE;
        delay_us(16);
    }
    GPIO_SetBits(GPIOB, GPIO_Pin_12 | GPIO_Pin_15);
    mode = ps2_led_debug.raw[1];
    ps2_led_debug.mode = mode;
    return (uint8_t)(ps2_led_debug.raw[2] == 0x5AU &&
                    (mode == 0x41U || mode == 0x73U || mode == 0x79U));
}

int main(void)
{
    uint8_t previous_down = 0;
    uint8_t armed = 0;
    uint8_t i;
    delay_init();
    led_init();
    ps2_init();
    ps2_led_debug.magic = 0x5053324CU;

    /* 上电闪三次，然后灭：确认测试程序已开始运行。 */
    for (i = 0; i < 3U; i++)
    {
        led_set(1);
        delay_ms(150);
        led_set(0);
        delay_ms(150);
    }

    while (1)
    {
        uint8_t valid = read_circle_frame();
        ps2_led_debug.loops++;
        ps2_led_debug.frame_valid = valid;
        if (valid != 0U)
        {
            /* PS2 原始按钮位：0=按下；取反后用 bit5 判断圆圈。 */
            uint8_t down = (uint8_t)((((uint8_t)~ps2_led_debug.raw[4]) & CIRCLE_MASK) != 0U);
            ps2_led_debug.valid_frames++;
            ps2_led_debug.circle_down = down;
            if (down == 0U)
                armed = 1;
            /* 只在“松开 -> 按下”时翻转，按住不会每 20ms 连续闪。 */
            if (armed != 0U && down != 0U && previous_down == 0U)
            {
                led_set((uint8_t)(ps2_led_debug.led_on == 0U));
                ps2_led_debug.circle_presses++;
            }
            previous_down = down;
        }
        else
        {
            ps2_led_debug.invalid_frames++;
            ps2_led_debug.circle_down = 0;
            previous_down = 0;
            armed = 0; /* 断线重连后，先松开圆圈再允许翻转。 */
        }
        delay_ms(20);
    }
}
