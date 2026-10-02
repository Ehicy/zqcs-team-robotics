#include "ps2.h"
#include "delay.h"
#include "stm32f10x.h"

#define PS2_DATA_PIN  GPIO_Pin_14
#define PS2_CMD_PIN   GPIO_Pin_15
#define PS2_CS_PIN    GPIO_Pin_12
#define PS2_CLK_PIN   GPIO_Pin_13
#define PS2_HALF_CLOCK_US  16U
#define PS2_BYTE_GAP_US    16U

/* 用命名引脚和标准库函数代替位带地址宏，便于对应接线。 */
void ps2_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_StructInit(&gpio);
    /* 先设置空闲电平，再切换输出模式，避免初始化时误选中接收器。 */
    GPIO_SetBits(GPIOB, PS2_CMD_PIN | PS2_CS_PIN | PS2_CLK_PIN);
    gpio.GPIO_Pin = PS2_CMD_PIN | PS2_CS_PIN | PS2_CLK_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin = PS2_DATA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
}

uint8_t ps2_comm(uint8_t send_data)
{
    uint8_t bit;
    uint8_t bit_mask;
    uint8_t received = 0;
    /* 固定循环 8 次，低位先传。原来依赖 uint8_t 左移溢出结束循环。 */
    for (bit = 0; bit < 8; bit++)
    {
        bit_mask = (uint8_t)(1U << bit);
        if ((send_data & bit_mask) != 0)
        {
            GPIO_SetBits(GPIOB, PS2_CMD_PIN);
        }
        else
        {
            GPIO_ResetBits(GPIOB, PS2_CMD_PIN);
        }
        GPIO_ResetBits(GPIOB, PS2_CLK_PIN);
        delay_us(PS2_HALF_CLOCK_US);
        if (GPIO_ReadInputDataBit(GPIOB, PS2_DATA_PIN) != 0)
        {
            received = (uint8_t)(received | bit_mask);
        }
        GPIO_SetBits(GPIOB, PS2_CLK_PIN);
        delay_us(PS2_HALF_CLOCK_US);
    }
    return received;
}

static uint8_t ps2_read_with_mode(ps2_data *data, uint8_t allow_digital)
{
    static const uint8_t request[PS2_FRAME_SIZE] =
        {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t received[PS2_FRAME_SIZE];
    uint8_t byte;
    if (data == 0)
    {
        return 0;
    }
    /* 无效帧也覆盖旧数据，不能让上一次的摇杆/按键继续生效。 */
    data->mode = 0;
    data->btn1 = 0;
    data->btn2 = 0;
    data->RJoy_LR = 128;
    data->RJoy_UD = 128;
    data->LJoy_LR = 128;
    data->LJoy_UD = 128;

    GPIO_SetBits(GPIOB, PS2_CMD_PIN | PS2_CLK_PIN);
    GPIO_ResetBits(GPIOB, PS2_CS_PIN);
    delay_us(PS2_HALF_CLOCK_US);
    for (byte = 0; byte < PS2_FRAME_SIZE; byte++)
    {
        received[byte] = ps2_comm(request[byte]);
        delay_us(PS2_BYTE_GAP_US);
    }
    GPIO_SetBits(GPIOB, PS2_CS_PIN | PS2_CMD_PIN);
    /* 第 0 字节是空闲响应；检查模式和固定标志，并非校验和。 */
    if ((received[1] != PS2_MODE_ANALOG &&
         !(allow_digital != 0U && received[1] == 0x41U)) || received[2] != 0x5AU)
    {
        return 0;
    }
    data->mode = received[1];
    data->btn1 = (uint8_t)(~received[3]);
    data->btn2 = (uint8_t)(~received[4]);
    if (received[1] == PS2_MODE_ANALOG)
    {
        data->RJoy_LR = received[5];
        data->RJoy_UD = received[6];
        data->LJoy_LR = received[7];
        data->LJoy_UD = received[8];
    }
    return 1;
}

uint8_t ps2_read(ps2_data *data)
{
    return ps2_read_with_mode(data, 0);
}

uint8_t ps2_read_buttons(ps2_data *data)
{
    return ps2_read_with_mode(data, 1);
}
