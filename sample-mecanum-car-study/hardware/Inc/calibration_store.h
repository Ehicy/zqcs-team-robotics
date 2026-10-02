#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H
#include <stdint.h>

/* C8的最后两个1KiB页；GNU/Keil均须将程序Flash限制为62KiB。 */
#define CAL_STORE_BASE       0x0800F800UL
#define CAL_STORE_PAGE_BYTES 1024U
#define CAL_STORE_BYTES      (2U * CAL_STORE_PAGE_BYTES)
/* v1的85/100试验已被用户否定；保留旧记录但不加载，首次启动用新默认值。 */
#define CAL_STORE_VERSION    2U
#define CAL_GAIN_MIN         50U
#define CAL_GAIN_MAX         100U
typedef struct
{
    uint8_t front_percent;
    uint8_t rear_percent;
} strafe_calibration_t;
enum { CAL_STORE_ERROR = 0, CAL_STORE_SAVED = 1, CAL_STORE_UNCHANGED = 2 };
uint8_t calibration_store_load(strafe_calibration_t *data);
/* 只允许调用方在电机输出全零时保存。追加记录、CRC、提交字最后写入。
 * 切页时只擦另一页，保留旧页的最后有效记录。
 */
uint8_t calibration_store_save(const strafe_calibration_t *data);
#endif
