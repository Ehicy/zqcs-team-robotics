#ifndef PS2_H
#define PS2_H
#include <stdint.h>

/* 与 PROJECT.md 的信号接线图一致。
 * DATA -> PB14（输入）；CMD -> PB15；CS/ATT -> PB12；CLK -> PB13。
 * 只接受 0x73 的 9 字节模拟模式，不支持 0x41 数字/0x79 压感模式。
 */
#define PS2_MODE_ANALOG  0x73U
#define PS2_FRAME_SIZE   9U

/* btn1 的 bit0～7 对应编号 0～7；btn2 对应 8～15。
 * 使用标准 PS2 名称，避免不同手柄的 A/B/X/Y 丝印差异。
 */
enum
{
    PS2_BUTTON_SELECT = 0, PS2_BUTTON_L3, PS2_BUTTON_R3, PS2_BUTTON_START,
    PS2_BUTTON_UP, PS2_BUTTON_RIGHT, PS2_BUTTON_DOWN, PS2_BUTTON_LEFT,
    PS2_BUTTON_L2, PS2_BUTTON_R2, PS2_BUTTON_L1, PS2_BUTTON_R1,
    PS2_BUTTON_TRIANGLE, PS2_BUTTON_CIRCLE, PS2_BUTTON_CROSS, PS2_BUTTON_SQUARE,
    PS2_BUTTON_COUNT
};

typedef struct
{
    uint8_t mode;
    uint8_t btn1;     /* 取反后的状态：1 按下，0 松开 */
    uint8_t btn2;
    uint8_t RJoy_LR;  /* 0 左，255 右，回中约 128 */
    uint8_t RJoy_UD;  /* 0 上，255 下 */
    uint8_t LJoy_LR;
    uint8_t LJoy_UD;
} ps2_data;

void ps2_init(void); /* 仅配置 GPIO；未实现模式协商，请在手柄上切换模式 */
uint8_t ps2_comm(uint8_t send_data);
/* 1：本帧模式/标志有效；0：无效，且数据已清成无按键、摇杆回中。 */
uint8_t ps2_read(ps2_data *data);
#endif
