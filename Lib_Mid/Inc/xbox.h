#ifndef XBOX_H
#define XBOX_H

#include <stddef.h>
#include <stdint.h>

#define XBOX_INPUT_REPORT_SIZE 20U
#define XBOX_INPUT_TRANSFER_MAX 64U

/* Xbox 报告解析错误；成功为 0，失败为负值。 */
typedef enum xbox_err_e {
    XBOX_ERR_OK = 0,
    XBOX_ERR_NULL = -1,
    XBOX_ERR_LENGTH = -2,
    XBOX_ERR_REPORT = -3
} xbox_err_t;

/* Xbox 360 兼容输入原始值；此类型与 HAL、RTOS 均无依赖。 */
typedef struct xbox_state_s {
    int16_t left_x;       /* 左摇杆 X，-32768 到 32767。 */
    int16_t left_y;       /* 左摇杆 Y，保留报告原始方向。 */
    int16_t right_x;      /* 右摇杆 X，-32768 到 32767。 */
    int16_t right_y;      /* 右摇杆 Y，保留报告原始方向。 */
    uint16_t buttons;     /* 原始按键位图，包含 Guide 位。 */
    uint8_t left_trigger; /* LT，0 到 255。 */
    uint8_t right_trigger;/* RT，0 到 255。 */
} xbox_state_t;

/* @brief 清零输入状态。
 * @param p_state 待清零状态，不能为空。
 * @return 0 成功，负值失败。 */
xbox_err_t Xbox_Reset(xbox_state_t *p_state);

/* @brief 从 00 14 开头的 USB 传输中解析标准 20 字节 Xbox 输入区。
 * @param p_data 完整输入报告，不能为空。
 * @param c_length 实际接收长度，20～64；实测接收器使用 32 字节传输。
 * @note 包头声明的标准区仍为 20 字节，后续厂商扩展数据不参与标准按键解析。
 * @param p_state 输出状态；解析失败时保持原值。
 * @return 0 成功，负值表示空参数、长度或报告类型错误。 */
xbox_err_t Xbox_Parse(const uint8_t *p_data, size_t c_length, xbox_state_t *p_state);

#endif
