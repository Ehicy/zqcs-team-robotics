#ifndef CALIBRATION_CONTROL_H
#define CALIBRATION_CONTROL_H
#include "ps2.h"
/* 无车身反馈传感器：按键是人工校准输入，不是自动跑偏检测。 */
void calibration_control_init(void);
void calibration_control_reset_inputs(void);
void calibration_control_reset_feedback(void);
/* 仅在已使能的有效模拟帧调用；返回1表示保存时已停机，调用方须重新回中使能。 */
uint8_t calibration_control_poll(const ps2_data *data);
/* 无阻塞保存提示：成功两闪、失败五闪；行驶时调用方取消提示。 */
uint8_t calibration_control_led(uint8_t *on);
extern volatile uint8_t calibration_loaded;
extern volatile uint8_t calibration_dirty;
extern volatile uint8_t calibration_learning; /* SELECT横移训练已通过门限 */
extern volatile uint8_t calibration_save_status; /* 1保存、2相同、3失败、4忽略运动中手动保存 */
#endif
